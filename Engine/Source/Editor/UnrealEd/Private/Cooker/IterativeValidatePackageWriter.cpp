// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/IterativeValidatePackageWriter.h"
#include "Cooker/CompactBinaryTCP.h"
#include "Cooker/CookGenerationHelper.h"
#include "Cooker/CookPackageData.h"
#include "Cooker/MPCollector.h"
#include "CookOnTheSide/CookLog.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Templates/UniquePtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogIterativeValidate, Log, All);

constexpr FStringView IterativeValidateFilename(TEXTVIEW("IterativeValidate.bin"));

class FIterativeValidateMPCollector : public UE::Cook::IMPCollector
{
public:
	FIterativeValidateMPCollector(FIterativeValidatePackageWriter* InOwner) : Owner(InOwner) {}

	virtual FGuid GetMessageType() const { return MessageType; }
	virtual const TCHAR* GetDebugName() const { return TEXT("IterativeValidateMPCollector"); }

	virtual void ServerTick(UE::Cook::FMPCollectorServerTickContext& Context) override;
	virtual void ClientTickPackage(UE::Cook::FMPCollectorClientTickPackageContext& Context) override;
	virtual void ServerTickPackage(UE::Cook::FMPCollectorServerTickPackageContext& Context);

	virtual void ClientReceiveMessage(UE::Cook::FMPCollectorClientMessageContext& Context, FCbObjectView Message) override;
	virtual void ServerReceiveMessage(UE::Cook::FMPCollectorServerMessageContext& Context, FCbObjectView Message) override;

public:
	static FGuid MessageType;

private:
	enum class EMessageSubtype : uint8
	{
		ServerToClient_WorkerStartup,
		ClientToServer_ReplIsAnotherSaveNeeded,
		ServerToClient_ReplUpdatePackageModificationStatus,
		Invalid
	};

	bool TryWritePackageStatus(FCbWriter& Writer, FName PackageName);
	void ReadAndSyncPackageStatus(FCbObjectView Message, FName PackageName);
	FIterativeValidatePackageWriter* Owner;
};

FCbWriter& operator<<(FCbWriter& Writer, const FIterativeValidatePackageWriter::FMessage& Message)
{
	Writer.BeginObject();
	Writer << "Text" << Message.Text;

	static_assert(sizeof(Message.Verbosity) == sizeof(uint8));
	Writer << "Verbosity" << (uint8)Message.Verbosity;

	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FIterativeValidatePackageWriter::FMessage& Message)
{
	bool bOk = !Field.HasError();
	if (bOk && LoadFromCompactBinary(Field["Text"], Message.Text))
	{
		uint8 Verbosity = ELogVerbosity::NumVerbosity;
		bOk = LoadFromCompactBinary(Field["Verbosity"], Verbosity) && Verbosity < ELogVerbosity::NumVerbosity;
		if (bOk)
		{
			Message.Verbosity = (ELogVerbosity::Type)Verbosity;
		}
	}
	if (!bOk)
	{
		Message = FIterativeValidatePackageWriter::FMessage();
	}
	return bOk;
}

FCbWriter& operator<<(FCbWriter& Writer, FIterativeValidatePackageWriter::EPackageStatus Status)
{
	Writer << (uint8)Status;
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FIterativeValidatePackageWriter::EPackageStatus& Status)
{
	uint8 StatusInteger = (uint8)FIterativeValidatePackageWriter::EPackageStatus::Count;
	if (!LoadFromCompactBinary(Field, StatusInteger))
	{
		UE_LOG(LogIterativeValidate, Error, TEXT("Failed to deserialize package status."));
	}
	else if (StatusInteger >= (uint8)FIterativeValidatePackageWriter::EPackageStatus::Count)
	{
		UE_LOG(LogIterativeValidate, Error, TEXT("Unexpected package status deserialized: %d"), StatusInteger);
	}
	else
	{
		Status = (FIterativeValidatePackageWriter::EPackageStatus)StatusInteger;
		return true;
	}
	return false;
}

FArchive& operator<<(FArchive& Ar, FIterativeValidatePackageWriter::FPackageStatusInfo& Info)
{
	Ar << Info.Status;
	bool bHasAssetClass = Info.AssetClass.IsValid();
	Ar << bHasAssetClass;
	if (bHasAssetClass)
	{
		Ar << Info.AssetClass;
	}
	return Ar;
}

FCbWriter& operator<<(FCbWriter& Writer, const FIterativeValidatePackageWriter::FPackageStatusInfo& Info)
{
	Writer.BeginArray();
	Writer << Info.Status;
	if (Info.AssetClass.IsValid())
	{
		Writer << Info.AssetClass;
	}
	Writer.EndArray();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FIterativeValidatePackageWriter::FPackageStatusInfo& Info)
{
	FCbArrayView ArrayField = Field.AsArrayView();
	if (ArrayField.Num() < 1)
	{
		return false;
	}
	FCbFieldViewIterator Iter = ArrayField.CreateViewIterator();
	bool bOk = true;
	bOk = LoadFromCompactBinary(*(Iter++), Info.Status) & bOk;
	if (ArrayField.Num() >= 2)
	{
		bOk = LoadFromCompactBinary(*(Iter++), Info.AssetClass) & bOk;
	}
	return bOk;
}

void FIterativeValidateMPCollector::ServerTick(UE::Cook::FMPCollectorServerTickContext& Context)
{
	static_assert(sizeof(EMessageSubtype) == sizeof(uint8));

	if (Context.GetEventType() == UE::Cook::FMPCollectorServerTickContext::EServerEventType::WorkerStartup)
	{
		FCbWriter Writer;
		Writer.BeginObject();
		Writer << "MessageSubtype" << (uint8)EMessageSubtype::ServerToClient_WorkerStartup;
		Writer << "PackageStatusMap" << Owner->PackageStatusMap;
		Writer << "PackageMessageMap" << Owner->PackageMessageMap;
		Writer.EndObject();
		Context.AddMessage(Writer.Save().AsObject());
	}
}

void FIterativeValidateMPCollector::ClientTickPackage(UE::Cook::FMPCollectorClientTickPackageContext& Context)
{
	static_assert(sizeof(EMessageSubtype) == sizeof(uint8));

	FCbWriter Writer;
	Writer.BeginObject();
	Writer << "MessageSubtype" << (uint8)EMessageSubtype::ClientToServer_ReplIsAnotherSaveNeeded;

	const FName PackageName = Context.GetPackageName();
	if (PackageName.IsNone())
	{
		UE_LOG(LogCook, Error, TEXT("Context does not contain a valid package name."))
		// It's safe to continue because TryWritePackageStatus will return false if the name is none. 
		// The error is logged here to make the call site clear
	}

	if (TryWritePackageStatus(Writer, PackageName))
	{
		Writer.EndObject();
		Context.AddMessage(Writer.Save().AsObject());
	}
}

void FIterativeValidateMPCollector::ServerTickPackage(UE::Cook::FMPCollectorServerTickPackageContext& Context)
{
	static_assert(sizeof(EMessageSubtype) == sizeof(uint8));

	FCbWriter Writer;
	Writer.BeginObject();
	Writer << "MessageSubtype" << (uint8)EMessageSubtype::ServerToClient_ReplUpdatePackageModificationStatus;

	const FName PackageName = Context.GetPackageName();
	if (PackageName.IsNone())
	{
		UE_LOG(LogCook, Error, TEXT("Context does not contain a valid package name."))
		// It's safe to continue because TryWritePackageStatus will return false if the name is none. 
		// The error is logged here to make the call site clear
	}

	if (TryWritePackageStatus(Writer, PackageName))
	{
		Writer.EndObject();
		Context.AddMessage(Writer.Save().AsObject());
	}
}

void FIterativeValidateMPCollector::ClientReceiveMessage(UE::Cook::FMPCollectorClientMessageContext& Context, FCbObjectView Message)
{
	uint8 MessageSubtypeAsInteger = (uint8)EMessageSubtype::Invalid;
	if (LoadFromCompactBinary(Message["MessageSubtype"], MessageSubtypeAsInteger))
	{
		switch ((EMessageSubtype)MessageSubtypeAsInteger)
		{
			case EMessageSubtype::ServerToClient_WorkerStartup:
			{
				bool bOk = LoadFromCompactBinary(Message["PackageStatusMap"], Owner->PackageStatusMap);
				bOk = bOk && LoadFromCompactBinary(Message["PackageMessageMap"], Owner->PackageMessageMap);
				check(bOk); // If we fail this, we will fail to get anything right during the rest of the validation. Better to terminate quickly.
				break;
			}
			case EMessageSubtype::ServerToClient_ReplUpdatePackageModificationStatus:
			{
				FName PackageName = Context.GetPackageName();
				if (!PackageName.IsNone())
				{
					ReadAndSyncPackageStatus(Message, PackageName);
				}
				else
				{
					UE_LOG(LogCook, Error, TEXT("Cannot process ServerToClient_ReplUpdatePackageModificationStatus without a valid package name in the current context."));
				}
				break;
			}
			default:
			{
				UE_LOG(LogCook, Error, TEXT("Unexpected message type: %d"), MessageSubtypeAsInteger);
				break;
			}
		};
	}
}

void FIterativeValidateMPCollector::ServerReceiveMessage(UE::Cook::FMPCollectorServerMessageContext& Context, FCbObjectView Message)
{
	uint8 MessageSubtypeAsInteger = (uint8)EMessageSubtype::Invalid;
	if (LoadFromCompactBinary(Message["MessageSubtype"], MessageSubtypeAsInteger))
	{
		FName PackageName = Context.GetPackageName();
		if (PackageName.IsNone())
		{
			UE_LOG(LogCook, Error, TEXT("Cannot process messages on server without a valid package name in the current context."));
		}
		else if (MessageSubtypeAsInteger == (uint8)EMessageSubtype::ClientToServer_ReplIsAnotherSaveNeeded)
		{
			ReadAndSyncPackageStatus(Message, PackageName);
		}
		else
		{
			UE_LOG(LogCook, Error, TEXT("Unexpected message received. MessageSubtype == %d"), MessageSubtypeAsInteger);
		}
	}
	else
	{
		UE_LOG(LogCook, Error, TEXT("Invalid message received. No MessageSubtype field available."));
	}
}

FGuid FIterativeValidateMPCollector::MessageType(TEXT("5E56C5D96F3B455E9452C15ADA601A71"));

bool FIterativeValidateMPCollector::TryWritePackageStatus(FCbWriter& Writer, FName PackageName)
{
	const FIterativeValidatePackageWriter::FPackageStatusInfo* PackageStatus
		= Owner->PackageStatusMap.Find(PackageName);

	if (PackageStatus && PackageStatus->Status != FIterativeValidatePackageWriter::EPackageStatus::NotYetProcessed)
	{
		Writer << "Status" << *PackageStatus;

		const TArray<FIterativeValidatePackageWriter::FMessage>* Messages = Owner->PackageMessageMap.Find(PackageName);
		if (Messages != nullptr)
		{
			Writer << "MessageArray" << *Messages;
		}
		return true;
	}
	return false;
}

void FIterativeValidateMPCollector::ReadAndSyncPackageStatus(FCbObjectView Message, FName PackageName)
{
	FIterativeValidatePackageWriter::FPackageStatusInfo Info;
	bool bOk = LoadFromCompactBinary(Message["Status"], Info);

	if (bOk)
	{
		Owner->PackageStatusMap.FindOrAdd(PackageName) = MoveTemp(Info);
		if (Message.FindView("MessageArray").HasValue())
		{
			TArray<FIterativeValidatePackageWriter::FMessage>& MessageArray = Owner->PackageMessageMap.FindOrAdd(PackageName);
			bOk = LoadFromCompactBinary(Message["MessageArray"], MessageArray);
		}
	}
	
	if (!bOk)
	{
		UE_LOG(LogCook, Error, TEXT("Invalid message received in ReadAndSyncPackageStatus. Failed to load Info from Message[\"Status\"] for package \"%s\""),
			*PackageName.ToString());
	}
}

FIterativeValidatePackageWriter::FIterativeValidatePackageWriter(UCookOnTheFlyServer& InCOTFS,
	TUniquePtr<ICookedPackageWriter>&& InInner, EPhase InPhase, const FString& ResolvedMetadataPath,
	UE::Cook::FDeterminismManager* InDeterminismManager)
	: FDiffPackageWriter(MoveTemp(InInner), InDeterminismManager)
	, MetadataPath(ResolvedMetadataPath)
	, COTFS(InCOTFS)
	, Phase(InPhase)
{
	COTFS.RegisterCollector(new FIterativeValidateMPCollector(this));

	Indent = FCString::Spc(FOutputDeviceHelper::FormatLogLine(ELogVerbosity::Warning,
		LogIterativeValidate.GetCategoryName(), TEXT(""), GPrintLogTimes).Len());

	TArray<FString> IterativeValidatePackageIgnoreList;
	GConfig->GetArray(TEXT("TargetDomain"), TEXT("IterativeValidatePackageIgnoreList"),
		IterativeValidatePackageIgnoreList, GEditorIni);
	PackageIgnoreList.Reserve(IterativeValidatePackageIgnoreList.Num());
	for (const FString& PackageName : IterativeValidatePackageIgnoreList)
	{
		PackageIgnoreList.Add(FName(FStringView(PackageName)));
	}
}

void FIterativeValidatePackageWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	bPackageFirstPass = true;
	switch (Phase)
	{
	case EPhase::AllInOnePhase:
		if (GetPackageStatus(Info.PackageName) == EPackageStatus::DeclaredUnmodified_NotYetProcessed)
		{
			// Save to memory and look for diffs before saving it out to disk
			SaveAction = ESaveAction::CheckForDiffs;
			Super::BeginPackage(Info);
		}
		else
		{
			// Not iteratively skippable, so we expect it to change. No need to look for diffs, just save to disk.
			if (bReadOnly)
			{
				SaveAction = ESaveAction::IgnoreResults;
			}
			else
			{
				SaveAction = ESaveAction::SaveToInner;
				Inner->BeginPackage(Info);
			}
		}
		break;
	case EPhase::Phase1:
		SaveAction = ESaveAction::CheckForDiffs;
		Super::BeginPackage(Info);
		break;
	case EPhase::Phase2:
		{
			EPackageStatus PackageStatus = GetPackageStatus(Info.PackageName);
			if (PackageStatus == EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified ||
				PackageStatus == EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList)
			{
				// Already saved in Phase 1; no need to diff it or save it now
				SaveAction = ESaveAction::IgnoreResults;
			}
			else if (PackageStatus == EPackageStatus::DeclaredUnmodified_FoundModified_IndeterminismOrFalsePositive)
			{
				SaveAction = ESaveAction::CheckForDiffs;
				Super::BeginPackage(Info);
			}
			else
			{
				// This is an IterativeModified package. It was found during Phase1 to be modified and would in a
				// normal iterative cook be resaved rather than iteratively skipped. Resave it as normal.
				SaveAction = ESaveAction::SaveToInner;
				Inner->BeginPackage(Info);
			}
			break;
		}
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::CommitPackage(FCommitPackageInfo&& Info)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::CommitPackage(MoveTemp(Info));
		break;
	case ESaveAction::SaveToInner:
		Inner->CommitPackage(MoveTemp(Info));
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::WritePackageData(const FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive,
	const TArray<FFileRegion>& FileRegions)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::WritePackageData(Info, ExportsArchive, FileRegions);
		break;
	case ESaveAction::SaveToInner:
		Inner->WritePackageData(Info, ExportsArchive, FileRegions);
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::WriteBulkData(const FBulkDataInfo& Info, const FIoBuffer& BulkData,
	const TArray<FFileRegion>& FileRegions)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::WriteBulkData(Info, BulkData, FileRegions);
		break;
	case ESaveAction::SaveToInner:
		Inner->WriteBulkData(Info, BulkData, FileRegions);
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::WriteAdditionalFile(Info, FileData);
		break;
	case ESaveAction::SaveToInner:
		Inner->WriteAdditionalFile(Info, FileData);
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}
void FIterativeValidatePackageWriter::WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info,
	 const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::WriteLinkerAdditionalData(Info, Data, FileRegions);
		break;
	case ESaveAction::SaveToInner:
		Inner->WriteLinkerAdditionalData(Info, Data, FileRegions);
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::WritePackageTrailer(const FPackageTrailerInfo& Info, const FIoBuffer& Data)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::WritePackageTrailer(Info, Data);
		break;
	case ESaveAction::SaveToInner:
		Inner->WritePackageTrailer(Info, Data);
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}

int64 FIterativeValidatePackageWriter::GetExportsFooterSize()
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		return Super::GetExportsFooterSize();
	case ESaveAction::SaveToInner:
		return Inner->GetExportsFooterSize();
	case ESaveAction::IgnoreResults:
		return 0;
	default:
		checkNoEntry();
		return 0;
	}
}

TUniquePtr<FLargeMemoryWriter> FIterativeValidatePackageWriter::CreateLinkerArchive(FName PackageName, UObject* Asset, uint16 MultiOutputIndex)
{
	if (Asset)
	{
		PackageStatusMap.FindOrAdd(PackageName).AssetClass = Asset->GetClass()->GetClassPathName();
	}

	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		return Super::CreateLinkerArchive(PackageName, Asset, MultiOutputIndex);
	case ESaveAction::SaveToInner:
		return Inner->CreateLinkerArchive(PackageName, Asset, MultiOutputIndex);
	case ESaveAction::IgnoreResults:
		return MakeUnique<FLargeMemoryWriter>();
	default:
		checkNoEntry();
		return TUniquePtr<FLargeMemoryWriter>();
	}
}

TUniquePtr<FLargeMemoryWriter> FIterativeValidatePackageWriter::CreateLinkerExportsArchive(FName PackageName, UObject* Asset, uint16 MultiOutputIndex)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		return Super::CreateLinkerExportsArchive(PackageName, Asset, MultiOutputIndex);
	case ESaveAction::SaveToInner:
		return Inner->CreateLinkerExportsArchive(PackageName, Asset, MultiOutputIndex);
	case ESaveAction::IgnoreResults:
		return MakeUnique<FLargeMemoryWriter>();
	default:
		checkNoEntry();
		return TUniquePtr<FLargeMemoryWriter>();
	}
}

bool FIterativeValidatePackageWriter::IsPreSaveCompleted() const
{
	return !bPackageFirstPass;
}

ICookedPackageWriter::FCookCapabilities FIterativeValidatePackageWriter::GetCookCapabilities() const
{
	FCookCapabilities Result = Super::GetCookCapabilities();
	Result.bReadOnly = bReadOnly;
	Result.bOverridesPackageModificationStatus = true;
	return Result;
}

void FIterativeValidatePackageWriter::Initialize(const FCookInfo& CookInfo)
{
	switch (Phase)
	{
	case EPhase::AllInOnePhase:
		if (CookInfo.bFullBuild)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display,
				TEXT("The cook is running non-iteratively. All packages are reported \"modified\" and will be resaved as in a normal cook."));
			bReadOnly = false;
		}
		else
		{
			bReadOnly = !FParse::Param(FCommandLine::Get(), TEXT("iterativevalidateallowwrite"));
		}
		break;
	case EPhase::Phase1:
		if (CookInfo.bFullBuild)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display,
				TEXT("The cook is running non-iteratively. All packages are reported \"modified\" and will be resaved during the final IterativeValidate phase."));
		}
		bReadOnly = false;
		break;
	case EPhase::Phase2:
		if (CookInfo.bFullBuild)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker, 
				LogIterativeValidate, Display,
				TEXT("The cook is running non-iteratively. Packages that were iteratively skipped and found valid will be resaved anyway."));
		}
		bReadOnly = false;
		break;
	default:
		checkNoEntry();
		break;
	}
	Super::Initialize(CookInfo);
}

void FIterativeValidatePackageWriter::UpdatePackageModificationStatus(FName PackageName, bool bIterativelyUnmodified,
	bool& bInOutShouldIterativelySkip)
{
	// We need to not skip previously cooked generator packages, if they were modified and we're read only we still
	// need to cook them so we can investigate their generated packages. Look up whether they were a generator in
	// the previous cook results.
	bool bKnownGenerator = false;
	if (bIterativelyUnmodified)
	{
		// GenerationHelpers were created for previously cooked generators by UCOTFS::PopulateCookedPackages.
		UE::Cook::FPackageData* PackageData = COTFS.PackageDatas->FindPackageDataByPackageName(PackageName);
		if (PackageData)
		{
			TRefCountPtr<UE::Cook::FGenerationHelper> GenerationHelper = PackageData->GetGenerationHelper();
			if (GenerationHelper)
			{
				bKnownGenerator = true;
			}
		}
	}

	switch (Phase)
	{
	case EPhase::AllInOnePhase:		
		// Save the input value for bIterativelyUnmodified, and report skippable for the modified if possible
		if (bIterativelyUnmodified)
		{
			SetPackageStatus(PackageName, EPackageStatus::DeclaredUnmodified_NotYetProcessed);
			bInOutShouldIterativelySkip = false;
		}
		else
		{
			SetPackageStatus(PackageName, EPackageStatus::DeclaredModified_WillNotVerify);
			if (!bKnownGenerator && bReadOnly)
			{
				bInOutShouldIterativelySkip = true;
			}
		}
		break;
	case EPhase::Phase1:
		// Save the iteratively unmodified packages to verify their diffs. Skip the non-generator iteratively
		// modified packages. Do not skip generator packages; we need to save them to test their generated packages.
		bInOutShouldIterativelySkip = !bIterativelyUnmodified && !bKnownGenerator;
		if (!bIterativelyUnmodified)
		{
			SetPackageStatus(PackageName, EPackageStatus::DeclaredModified_WillNotVerify);
		}
		else
		{
			SetPackageStatus(PackageName, EPackageStatus::DeclaredUnmodified_NotYetProcessed);
		}
		break;
	case EPhase::Phase2:
	{
		// Ignore the Unmodified flag from this cook phase. Skip the packages that were found to 
		// be IterativeValidated from Phase1. Save the packages that phase1 found modified; this phase
		// is responsible for getting those resaved. Reexecute save for the packages that were IterativeFailed
		// from phase1, so we can test whether they are indeterministic. Always save generators so we can
		// test their generated packages.
		EPackageStatus PackageStatus = GetPackageStatus(PackageName);
		bInOutShouldIterativelySkip =
			(PackageStatus == EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified
				|| PackageStatus == EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList)
			&& !bKnownGenerator;
		break;
	}
	default:
		checkNoEntry();
		break;
	}

	bool bInnerIterativelyUnmodified = bInOutShouldIterativelySkip;
	bool bInnerInOutShouldIterativelySkip = bInOutShouldIterativelySkip;
	Inner->UpdatePackageModificationStatus(PackageName, bInnerIterativelyUnmodified, bInnerInOutShouldIterativelySkip);
	checkf(bInnerInOutShouldIterativelySkip == bInnerIterativelyUnmodified,
		TEXT("IterativeValidatePackageWriter is not supported with an Inner that modifies bInOutShouldIterativelySkip."));
}

void FIterativeValidatePackageWriter::BeginCook(const FCookInfo& Info)
{
	switch (Phase)
	{
	case EPhase::AllInOnePhase:
		if (bReadOnly)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display,
				TEXT("-IterativeValidateAllowWrite not present, read-only mode. Running -diffonly on all packages that were found to be iteratively unmodified."));
		}
		else
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display,
				TEXT("-IterativeValidateAllowWrite is present, writable mode. Resaving packages as in a normal cook, but also running -diffonly on all packages that were found to be iteratively unmodified."));
		}
		if (Info.bFullBuild)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Error,
				TEXT("IterativeValidate was bypassed on this run; it is a full cook and all packages are marked iteratively modified."));
		}
		break;
	case EPhase::Phase1:
		UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
			LogIterativeValidate, Display,
			TEXT("Phase1: running -diffonly and a resave on all packages discovered to be iteratively unmodified."));
		if (Info.bFullBuild)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Error,
				TEXT("IterativeValidate was bypassed on this run; it is a full cook and all packages are marked iteratively modified."));
		}
		break;
	case EPhase::Phase2:
		{
			Load();
			FStatusCounts StatusCounts = CountPackagesByStatus();
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display,
				TEXT("Phase2: %d packages were found during Phase1 to be iteratively unmodified but had differences. "
					"Running -diffonly on them again to check whether the differences are due to indeterminism or to IterativeFalsePositives."), 
					StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_IndeterminismOrFalsePositive]);
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display,
				TEXT("%d packages were found during Phase1 to be modified or new and will be resaved."),
				StatusCounts[EPackageStatus::DeclaredModified_WillNotVerify]);
			break;
		}
	default:
		checkNoEntry();
		break;
	}
	Super::BeginCook(Info);
}

void FIterativeValidatePackageWriter::EndCook(const FCookInfo& Info)
{
	Super::EndCook(Info);
	FStatusCounts StatusCounts = CountPackagesByStatus();

	switch (Phase)
	{
	case EPhase::AllInOnePhase:
	{
		int32 DetectedUnmodified = StatusCounts[EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList];
		UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
			LogIterativeValidate, Display,
			TEXT("Modified: %d. DetectedUnmodified: %d. ValidatedUnmodified: %d. IterativeSkipFalsePositive: %d."),
			StatusCounts[EPackageStatus::DeclaredModified_WillNotVerify], 
			DetectedUnmodified,
			StatusCounts[EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList],
			StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]);
		FString Message = FString::Printf(TEXT("Packages Iteratively Skipped: %d: IterativeSkipFalsePositive: %d."),
			DetectedUnmodified,
			StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]);
		if (StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive] > 0)
		{
			TStringBuilder<1024> MessageWithDiagnostics;
			MessageWithDiagnostics << Message;
			TMap<FTopLevelAssetPath, int32> ClassFalsePositiveCounts = GetSummaryFalsePositiveCounts();
			int32 NumPrinted = 0;
			constexpr int32 MaxNumPrinted = 5;
			for (const TPair<FTopLevelAssetPath, int32>& Pair : ClassFalsePositiveCounts)
			{
				MessageWithDiagnostics << TEXT("\n\t") << WriteToString<256>(Pair.Key) << TEXT(": ") << Pair.Value;
				if (++NumPrinted >= MaxNumPrinted)
				{
					break;
				}
			}
			if (ClassFalsePositiveCounts.Num() > NumPrinted)
			{
				MessageWithDiagnostics << TEXT("\n\t...");
			}
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Error, TEXT("%s"), *MessageWithDiagnostics);
		}
		else if (StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList] > 0)
		{
			Message = FString::Printf(TEXT("Packages Iteratively Skipped: %d: IterativeSkipFalsePositive (Ignored): %d."),
				DetectedUnmodified,
				StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList]);
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Warning, TEXT("%s"), *Message);
		}
		else
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display, TEXT("%s"), *Message);
		}
		break;
	}
	case EPhase::Phase1:
	{
		int32 DetectedUnmodified = StatusCounts[EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_IndeterminismOrFalsePositive]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList];
		UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
			LogIterativeValidate, Display,
			TEXT("Modified: %d. DetectedUnmodified: %d. ValidatedUnmodified: %d. IterativeSkipFalsePositiveOrIndeterminism: %d."),
			StatusCounts[EPackageStatus::DeclaredModified_WillNotVerify],
			DetectedUnmodified,
			StatusCounts[EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList],
			StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_IndeterminismOrFalsePositive]);
		Save();
		break;
	}
	case EPhase::Phase2:
	{
		int32 DetectedUnmodified = StatusCounts[EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_IndeterminismOrFalsePositive]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_Indeterminism]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList];
		UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
			LogIterativeValidate, Display,
			TEXT("Modified: %d. DetectedUnmodified: %d. ValidatedUnmodified: %d. Indeterminism: %d. IterativeSkipFalsePositive: %d."),
			StatusCounts[EPackageStatus::DeclaredModified_WillNotVerify],
			DetectedUnmodified,
			StatusCounts[EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified]
			+ StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList],
			StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_Indeterminism],
			StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]
			);
		FString Message = FString::Printf(TEXT("Packages Iteratively Skipped: %d: IterativeSkipFalsePositive: %d."),
			DetectedUnmodified,
			StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]);
		if (StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive] > 0)
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Error, TEXT("%s"), *Message);
		}
		else if (StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList] > 0)
		{
			Message = FString::Printf(TEXT("Packages Iteratively Skipped: %d: IterativeSkipFalsePositive (Ignored): %d."),
				DetectedUnmodified,
				StatusCounts[EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive]);
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Warning, TEXT("%s"), *Message);
		}
		else
		{
			UE_CLOG(COTFS.GetCookMode() != ECookMode::CookWorker,
				LogIterativeValidate, Display, TEXT("%s"), *Message);
		}
		break;
	}
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::UpdateSaveArguments(FSavePackageArgs& SaveArgs)
{
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		Super::UpdateSaveArguments(SaveArgs);
		break;
	case ESaveAction::SaveToInner:
		Inner->UpdateSaveArguments(SaveArgs);
		break;
	case ESaveAction::IgnoreResults:
		break;
	default:
		checkNoEntry();
		break;
	}
}

bool FIterativeValidatePackageWriter::IsAnotherSaveNeeded(FSavePackageResultStruct& PreviousResult, FSavePackageArgs& SaveArgs)
{
	bPackageFirstPass = false;
	checkf(!Inner->IsAnotherSaveNeeded(PreviousResult, SaveArgs),
		TEXT("IterativeValidatePackageWriter does not support an Inner that needs multiple saves."));
	if (PreviousResult == ESavePackageResult::Timeout)
	{
		return false;
	}
	switch (SaveAction)
	{
	case ESaveAction::CheckForDiffs:
		break;
	case ESaveAction::SaveToInner:
		// The SaveToInner pass, if present, is the last pass in a phase
		return false;
	case ESaveAction::IgnoreResults:
		// The IgnoreResults pass, if present, is the last pass in a phase
		return false;
	default:
		checkNoEntry();
		break;
	}

	switch (Phase)
	{
	case EPhase::AllInOnePhase:
		check(GetPackageStatus(BeginInfo.PackageName) == EPackageStatus::DeclaredUnmodified_NotYetProcessed); // Otherwise we would have set SaveAction=SaveToInner or IgnoreResults and early exited above
		if (Super::IsAnotherSaveNeeded(PreviousResult, SaveArgs))
		{
			return true;
		}
		else
		{
			// Once our superclass has finished looking for differences, finish it off and start a SaveToInner pass
			FCommitPackageInfo CommitInfo;
			CommitInfo.Status = IPackageWriter::ECommitStatus::Success;
			CommitInfo.PackageName = BeginInfo.PackageName;
			CommitInfo.WriteOptions = EWriteOptions::None;
			Super::CommitPackage(MoveTemp(CommitInfo));

			if (bIsDifferent && !bNewPackage)
			{
				if (!PackageIgnoreList.Contains(BeginInfo.PackageName))
				{
					SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive);
				}
				else
				{
					SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList);
				}
			}
			else if (!bNewPackage)
			{

				SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified);
			}
			else
			{
				SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredModified_WillNotVerify);
			}

			if (bReadOnly)
			{
				SaveAction = ESaveAction::IgnoreResults;
				return false;
			}
			else
			{
				Inner->BeginPackage(BeginInfo);
				SaveAction = ESaveAction::SaveToInner;
				return true;
			}
		}
	case EPhase::Phase1:
		if (Super::IsAnotherSaveNeeded(PreviousResult, SaveArgs))
		{
			return true;
		}
		else if (bIsDifferent && !bNewPackage)
		{
			// If our superclass FDiffPackageWriter found differences, when it finishes the saves it wants to do,
			// Finish it off and start a SaveToInner pass
			FCommitPackageInfo CommitInfo;
			CommitInfo.Status = IPackageWriter::ECommitStatus::Success;
			CommitInfo.PackageName = BeginInfo.PackageName;
			CommitInfo.WriteOptions = EWriteOptions::None;
			Super::CommitPackage(MoveTemp(CommitInfo));
			Inner->BeginPackage(BeginInfo);
			SaveAction = ESaveAction::SaveToInner;

			if (!PackageIgnoreList.Contains(BeginInfo.PackageName))
			{
				// Mark that the iterative validation failed if it was not already marked by log or warning messages.
				// We need to record it for an indeterminism test
				SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_FoundModified_IndeterminismOrFalsePositive);
			}
			else
			{
				SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_FoundModified_OnIgnoreList);
			}

			return true;
		}
		else if (!bNewPackage)
		{
			// No differences found, so finish off the superclass's save during CommitPackage, without doing a
			// SaveToInner pass
			// Mark that the iterative validation passed
			SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified);

			TArray<FMessage> Messages;
			PackageMessageMap.RemoveAndCopyValue(BeginInfo.PackageName, Messages);
			for (FMessage& Message : Messages)
			{
				// If no differences were detected, we should not have logged any warning or error messages
				check(Message.Verbosity > ELogVerbosity::Warning);
			}
			return false;
		}
		else
		{
			// New packages need to be resaved in Phase2; for our purposes they are equivalent
			// to a package that iteration detected as modified.
			// Do not add an entry for it in our results for iterative packages, and do not resave it in this pass
			SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredModified_WillNotVerify);
			return false;
		}
	case EPhase::Phase2:
		LogIterativeDifferences();
		// No need to do the Super's second diff pass to find callstacks - just knowing whether differences exist is enough.
		// No need to save package to disk; for these packages (packages found to be iteratively unmodified during
		// Phase1) they were already resaved during Phase1
		return false;
	default:
		checkNoEntry();
		return false;
	}
}

void FIterativeValidatePackageWriter::OnDiffWriterMessage(ELogVerbosity::Type Verbosity, FStringView Message)
{
	switch (Phase)
	{
	case EPhase::AllInOnePhase:
		FMsg::Logf(__FILE__, __LINE__, LogIterativeValidate.GetCategoryName(), Verbosity,
			TEXT("%s"), *ResolveText(Message));
		break;
	case EPhase::Phase1:
		PackageMessageMap.FindOrAdd(BeginInfo.PackageName).Add(FMessage{ FString(Message), Verbosity });
		break;
	case EPhase::Phase2:
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FIterativeValidatePackageWriter::LogIterativeDifferences()
{
	// This function is called during Phase2 for packages that had differences during Phase1.
	// It is called immediately after first-pass package save that is run by our super class FDiffPackageWriter, which
	// compared it against the version that was written to disk during Phase1. If there are differences
	// now from Phase1, then this package has a determinism issue. We log that information at display rather
	// than Warning because this cookmode only logs warnings for IterativeSkipFalsePositives.
	bool bHasDeterminismIssue = bIsDifferent;
	if (bHasDeterminismIssue)
	{
		UE_LOG(LogIterativeValidate, Display, TEXT("Could not validate %s because it has a non-deterministic save."),
			*BeginInfo.PackageName.ToString());
		SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_FoundModified_Indeterminism);
		return;
	}

	// Otherwise, no determinism issues, so the differences indicate a bug in Diff Package
	SetPackageStatus(BeginInfo.PackageName, EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive);
	FMsg::Logf(__FILE__, __LINE__, LogIterativeValidate.GetCategoryName(), ELogVerbosity::Warning,
		TEXT("IterativeSkipFalsePositive package %s."), *BeginInfo.PackageName.ToString());
	TArray<FMessage>& Messages = PackageMessageMap.FindOrAdd(BeginInfo.PackageName);
	for (const FMessage& Message : Messages)
	{
		FMsg::Logf(__FILE__, __LINE__, LogIterativeValidate.GetCategoryName(), Message.Verbosity,
			TEXT("%s"), *ResolveText(Message.Text));
	}
}

void FIterativeValidatePackageWriter::Save()
{
	FString IterativeValidatePath = GetIterativeValidatePath();
	TUniquePtr<FArchive> DiskArchive(IFileManager::Get().CreateFileWriter(*IterativeValidatePath));

	if (!DiskArchive)
	{
		UE_LOG(LogIterativeValidate, Error,
			TEXT("Could not write to file %s. This file is needed to store results for the -IterativeValidate cook."),
			*IterativeValidatePath);
		return;
	}
	FNameAsStringProxyArchive Ar(*DiskArchive);
	Serialize(Ar);
}

void FIterativeValidatePackageWriter::Load()
{
	FString IterativeValidatePath = GetIterativeValidatePath();
	TUniquePtr<FArchive> DiskArchive(IFileManager::Get().CreateFileReader(*IterativeValidatePath));
	if (!DiskArchive)
	{
		UE_LOG(LogIterativeValidate, Fatal,
			TEXT("Could not load file %s. This file is required and should have been written by the -IterativeValidatePhase1 cook."),
			*IterativeValidatePath);
		return;
	}
	FNameAsStringProxyArchive Ar(*DiskArchive);
	Serialize(Ar);
	if (Ar.IsError())
	{
		UE_LOG(LogIterativeValidate, Fatal, TEXT("Corrupt file %s"), *IterativeValidatePath);
	}
}

void FIterativeValidatePackageWriter::Serialize(FArchive& Ar)
{
	constexpr int32 LatestVersion = 0;
	int32 Version = LatestVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != LatestVersion)
	{
		Ar.SetError();
		return;
	}
	Ar << PackageStatusMap;
	Ar << PackageMessageMap;
}

FArchive& operator<<(FArchive& Ar, FIterativeValidatePackageWriter::FMessage& Message)
{
	uint8 Verbosity = static_cast<uint8>(Message.Verbosity);
	Ar << Verbosity << Message.Text;
	if (Ar.IsLoading())
	{
		Message.Verbosity = static_cast<ELogVerbosity::Type>(Verbosity);
	}
	return Ar;
}

FString FIterativeValidatePackageWriter::GetIterativeValidatePath() const
{
	return FPaths::Combine(MetadataPath, FString(IterativeValidateFilename));
}

FIterativeValidatePackageWriter::EPackageStatus FIterativeValidatePackageWriter::GetPackageStatus(FName PackageName) const
{
	if (const FPackageStatusInfo* Info = PackageStatusMap.Find(PackageName))
	{
		return Info->Status;
	}
	return EPackageStatus::NotYetProcessed;
}

void FIterativeValidatePackageWriter::SetPackageStatus(FName PackageName, EPackageStatus NewStatus)
{
	FPackageStatusInfo& Info = PackageStatusMap.FindOrAdd(PackageName);
	Info.Status = NewStatus;
	switch (NewStatus)
	{
	case EPackageStatus::DeclaredUnmodified_ConfirmedUnmodified: [[fallthrough]];
	case EPackageStatus::DeclaredModified_WillNotVerify:
		// For the non-error cases, clear the AssetClass so that we don't waste bandwidth or diskspace to store it.
		Info.AssetClass = FTopLevelAssetPath();
		break;
	default:
		break;
	}
}

FIterativeValidatePackageWriter::FStatusCounts FIterativeValidatePackageWriter::CountPackagesByStatus()
{
	FIterativeValidatePackageWriter::FStatusCounts StatusCounts;

	for (const TPair<FName,FPackageStatusInfo>& Pair : PackageStatusMap)
	{
		StatusCounts[Pair.Value.Status]++;
	}

	return StatusCounts;
}

TMap<FTopLevelAssetPath, int32> FIterativeValidatePackageWriter::GetSummaryFalsePositiveCounts()
{
	TMap<FTopLevelAssetPath, int32> Result;
	for (const TPair<FName, FPackageStatusInfo>& Pair : PackageStatusMap)
	{
		if (Pair.Value.Status == EPackageStatus::DeclaredUnmodified_FoundModified_FalsePositive
			&& Pair.Value.AssetClass.IsValid())
		{
			++(Result.FindOrAdd(Pair.Value.AssetClass, 0));
		}
	}

	Result.ValueSort([](const int32& A, const int32& B)
		{
			return A > B;
		});
	return Result;
}
