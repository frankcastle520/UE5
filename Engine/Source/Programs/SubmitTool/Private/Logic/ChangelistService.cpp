// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChangelistService.h"

#include "Configuration/Configuration.h"
#include "Logging/SubmitToolLog.h"
#include "Logic/Services/Interfaces/ISTSourceControlService.h"
#include "Models/ModelInterface.h"
#include "Telemetry/TelemetryService.h"
#include "CommandLine/CmdLineParameters.h"

#include "ISourceControlProvider.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "SourceControlResultInfo.h"

THIRD_PARTY_INCLUDES_START
// used to retrieve the depot paths, UE hides all this p4 specific information under PerforceSourceControl private definitions
#include <p4/clientapi.h>
THIRD_PARTY_INCLUDES_END

namespace ChangelistServiceConstants {
	constexpr float TickDelay = 5.f;
}

FChangelistService::FChangelistService(
	const FGeneralParameters& InParameters,
	const TSharedPtr<ISTSourceControlService> InPerforceService,
	const FOnChangeListReadyDelegate& InCLReadyCallback,
	const FOnChangelistRefreshDelegate& InCLRefreshCallback) :
	Parameters(InParameters),
	CLReadyCallback(InCLReadyCallback),
	CLRefreshCallback(InCLRefreshCallback),
	TickHandle(FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FChangelistService::P4Tick), ChangelistServiceConstants::TickDelay)),
	SourceControlService(InPerforceService)
{
	FCmdLineParameters::Get().GetValue(FSubmitToolCmdLine::P4ChangeList, CLID);
	Init();
}

FChangelistService::~FChangelistService()
{
	OnCLDescriptionUpdated.Clear();
}

void FChangelistService::Init()
{
	if(SourceControlService->GetProvider().IsValid())
	{
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this] {
			GetFilesDepotPaths(true);
		});

		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this] {
			GetShelvedFilesDepotPaths(true);
		});

		CachedSCCProvider = SourceControlService->GetProvider().Get();

		if(!CLID.IsNumeric() && CLID.Equals(TEXT("default"), ESearchCase::IgnoreCase))
		{
			CreateCLFromDefaultCL();
		}
		else
		{
			FindInitialChangelistsAsync();
		}
		
	}
	else
	{
		UE_LOG(LogSubmitToolP4, Error, TEXT("Perforce Connection was invalid"));
	}
}

void FChangelistService::FindInitialChangelistsAsync()
{
	TSharedRef<FUpdatePendingChangelistsStatus> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
	UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);

	UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Finding available changelists"));

	ActiveP4Operations.Add(UpdatePendingChangelistsOperation);

	CachedSCCProvider->Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda([this](const FSourceControlOperationRef& UpdateOperation, ECommandResult::Type Result)
		{
			if(Result == ECommandResult::Succeeded)
			{
				TArray<FSourceControlChangelistRef> Changelists = CachedSCCProvider->GetChangelists(EStateCacheUsage::Use);
				for(const FSourceControlChangelistRef& CL : Changelists)
				{
					if(CL->GetIdentifier() == CLID)
					{
						ChangelistPtr = CL;
					}
				}
			}
			else if(Result == ECommandResult::Cancelled)
			{
				UE_LOG(LogSubmitToolP4, Warning, TEXT("Cancelled finding available changelists"));
			}
			else if(Result == ECommandResult::Failed)
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("Failed to find available changelists."));
				PrintErrorMessages(UpdateOperation->GetResultInfo());
			}

			ActiveP4Operations.Remove(UpdateOperation);

			if(ChangelistPtr)
			{
				// Get state for our CL
				FetchChangelistDataAsync();

				// Fire & Forget updating all CLs
				TSharedRef<FUpdatePendingChangelistsStatus> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
				UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);
				UpdatePendingChangelistsOperation->SetUpdateShelvedFilesStates(true);
				UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);
				CachedSCCProvider->Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous);
			}
			else
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("Couldn't retrieve information from CL %s"), *CLID);
				FModelInterface::SetErrorState();
			}
		}));
}

void FChangelistService::FetchChangelistDataAsync()
{
	TSharedRef<FUpdatePendingChangelistsStatus> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
	UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);
	UpdatePendingChangelistsOperation->SetUpdateShelvedFilesStates(true);

	if(ChangelistPtr.IsValid())
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Updating CL %s changes from P4"), *GetCLID());
		UpdatePendingChangelistsOperation.Get().SetChangelistsToUpdate(TArray{ ChangelistPtr.ToSharedRef() });
	}
	else
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Retrieving CL %s information from P4"), *CLID);
		UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);
	}

	ActiveP4Operations.Add(UpdatePendingChangelistsOperation);

	CachedSCCProvider->Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateRaw(this, &FChangelistService::FetchChangelistCallback));
}


void FChangelistService::FetchChangelistCallback(const FSourceControlOperationRef& UpdateOperation, ECommandResult::Type Result)
{
	if(Result == ECommandResult::Succeeded)
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Update operation succeeded."));
		PrintMessages(UpdateOperation->GetResultInfo());

		// If this is the first time we fetch results from perforce, we cache our CL State object and let the app know 
		if(!ChangelistState.IsValid())
		{
			TArray<FSourceControlChangelistRef> Changelists = CachedSCCProvider->GetChangelists(EStateCacheUsage::Use);
			for(const FSourceControlChangelistRef& CL : Changelists)
			{
				if(CL->GetIdentifier() == CLID)
				{
					ChangelistState = CachedSCCProvider->GetState(ChangelistPtr.ToSharedRef(), EStateCacheUsage::Use);
				}
			}

			if(!ChangelistPtr.IsValid())
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("Failed to fetch CL %s from P4"), *CLID);
			}
			else
			{
				OriginalDescription = ChangelistState->GetDescriptionText();
				CLDescription = ChangelistState->GetDescriptionText().ToString();
				FilesInCL = ChangelistState->GetFilesStates();
				ShelvedFilesInCL = ChangelistState->GetShelvedFilesStates();
				PrintFilesAndShelvedFiles();
			}

			CLReadyCallback.ExecuteIfBound(ChangelistState.IsValid());
		}
		else
		{
			RehydrateDataFromP4State();
		}
	}
	else if(Result == ECommandResult::Cancelled)
	{
		UE_LOG(LogSubmitToolP4, Warning, TEXT("Update operation cancelled."));
	}
	else if(Result == ECommandResult::Failed)
	{
		UE_LOG(LogSubmitToolP4, Warning, TEXT("Update operation failed."));
		PrintErrorMessages(UpdateOperation->GetResultInfo());
	}

	ActiveP4Operations.Remove(UpdateOperation);
}

void FChangelistService::RevertUnchangedFilesAsync(const FSourceControlOperationComplete& OnRevertComplete)
{
	UE_LOG(LogSubmitToolP4, Log, TEXT("Reverting unchanged files from CL %s..."), *GetCLID());

	TSharedRef<FRevertUnchanged> RevertUnchangedOperation = ISourceControlOperation::Create<FRevertUnchanged>();

	ActiveP4Operations.Add(RevertUnchangedOperation);

	CachedSCCProvider->Execute(RevertUnchangedOperation, ChangelistPtr, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[this, OnRevertComplete](const FSourceControlOperationRef& Operation, ECommandResult::Type Result) {

			// Revert Unchanged returns as failed if there were no files to revert, check ErrorMessages to see actual failures
			if(Result == ECommandResult::Cancelled)
			{
				UE_LOG(LogSubmitToolP4, Warning, TEXT("Revert unchanged operation cancelled."));
			}
			else if(Result == ECommandResult::Succeeded)
			{
				UE_LOG(LogSubmitToolP4, Log, TEXT("Revert unchanged operation succeeded."));
				FilesInCL = ChangelistState->GetFilesStates();
				FilesDepotPaths.Reset(FilesInCL.Num());
				PrintFilesAndShelvedFiles();
			}
			else if(Operation->GetResultInfo().ErrorMessages.Num() == 0)
			{
				UE_LOG(LogSubmitToolP4, Log, TEXT("There were no unchanged files to revert."));
			}
			else if(Result == ECommandResult::Failed)
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("CL %s revert unchanged operation failed:"), *GetCLID());
				PrintErrorMessages(Operation->GetResultInfo());
			}

			ActiveP4Operations.Remove(Operation);
			OnRevertComplete.ExecuteIfBound(Operation, Result);
		}
	));
}

void FChangelistService::RehydrateDataFromP4State()
{
	ETaskArea ChangeType = ETaskArea::None;

	if(!AreCLDescriptionsIdentical())
	{
		ChangeType |= ETaskArea::Changelist;

		UE_LOG(LogSubmitToolP4, Log, TEXT("CL %s Description was updated outside of Submit Tool while it was still open, Description has been updated to match P4V."), *GetCLID());
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("\n - Original Description '%s'\n - Submit Tool Description '%s'\n - New Description '%s'"), *OriginalDescription.ToString(), *CLDescription, *ChangelistState->GetDescriptionText().ToString());

		OriginalDescription = ChangelistState->GetDescriptionText();
		CLDescription = ChangelistState->GetDescriptionText().ToString();
	}

	if(ChangelistState->GetFilesStatesNum() != FilesInCL.Num())
	{
		ChangeType |= ETaskArea::LocalFiles;
		FilesInCL = ChangelistState->GetFilesStates();
		FilesDepotPaths.Reset(FilesInCL.Num());
		UE_LOG(LogSubmitToolP4, Log, TEXT("CL %s files were updated outside of Submit Tool while it was open, Validation state has been reset"), *GetCLID());
		PrintFilesAndShelvedFiles();
	}

	if(ChangelistState->GetShelvedFilesStatesNum() != ShelvedFilesInCL.Num())
	{
		ChangeType |= ETaskArea::ShelvedFiles;
		ShelvedFilesInCL = ChangelistState->GetShelvedFilesStates();
		ShelvedFilesDepotPaths.Reset(ShelvedFilesInCL.Num());
		UE_LOG(LogSubmitToolP4, Log, TEXT("CL %s shelved files were updated outside of Submit Tool while it was open, Validation state has been reset"), *GetCLID());
		PrintFilesAndShelvedFiles();
	}

	if(ChangeType != ETaskArea::None)
	{
		CLRefreshCallback.ExecuteIfBound(ChangeType);
	}
}

bool FChangelistService::AreCLDescriptionsIdentical()
{
	const FString NewDescription = ChangelistState->GetDescriptionText()
		.ToString()
		.TrimStartAndEnd()
		.TrimChar('\n');

	const FString CurrentDescription = OriginalDescription
		.ToString()
		.TrimStartAndEnd()
		.TrimChar('\n');

	return NewDescription.Equals(CurrentDescription, ESearchCase::IgnoreCase);
}

void FChangelistService::PrintMessages(const FSourceControlResultInfo& ResultInfo) const
{
	for(const FText& Msg : ResultInfo.InfoMessages)
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("%s"), *Msg.ToString());
	}
}

void FChangelistService::PrintErrorMessages(const FSourceControlResultInfo& ResultInfo) const
{
	for(const FText& errorMsg : ResultInfo.ErrorMessages)
	{
		UE_LOG(LogSubmitToolP4, Error, TEXT("%s"), *errorMsg.ToString());
	}
}

void FChangelistService::Submit(const FString& InDescriptionAddendum, const FSourceControlOperationComplete& OnSubmitComplete)
{
	UE_LOG(LogSubmitToolP4, Warning, TEXT("Submit in progress for CL: %s. Please wait..."), *GetCLID());

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> CheckinOp = ISourceControlOperation::Create<FCheckIn>();

	FString FinalDescription = FString::Format(TEXT("{0}{1}"), { CLDescription, InDescriptionAddendum });

	CheckinOp->SetDescription(FText::FromString(FinalDescription));

	ActiveP4Operations.Add(CheckinOp);

	CachedSCCProvider->Execute(CheckinOp, ChangelistPtr, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[this, OnSubmitComplete](const FSourceControlOperationRef& Operation, ECommandResult::Type Result) {
			if(Result == ECommandResult::Failed)
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("CL %s submit operation failed:"), *GetCLID());
				PrintErrorMessages(Operation->GetResultInfo());
			}
			else if(Result == ECommandResult::Succeeded)
			{
				TSharedRef<FCheckIn> CheckIn = StaticCastSharedRef<FCheckIn>(Operation);
				UE_LOG(LogSubmitToolP4, Log, TEXT("Submit operation succeeded: %s"), *CheckIn->GetSuccessMessage().ToString());

				FTelemetryService::Get()->SubmitSucceeded();
			}
			else if(Result == ECommandResult::Cancelled)
			{
				UE_LOG(LogSubmitToolP4, Warning, TEXT("Submit operation cancelled."));
			}

			ActiveP4Operations.Remove(Operation);
			OnSubmitComplete.ExecuteIfBound(Operation, Result);
		}
	));
}

void FChangelistService::SendCLDescriptionToP4(EConcurrency::Type Concurrency, FOnP4OperationCompleteDelegate InCallback)
{
	if(!CLDescription.Equals(OriginalDescription.ToString()))
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Saving CL %s"), *GetCLID());
		TSharedRef<FEditChangelist, ESPMode::ThreadSafe> EditCLOp = ISourceControlOperation::Create<FEditChangelist>();
		EditCLOp->SetDescription(FText::FromString(CLDescription));

		ActiveP4Operations.Add(EditCLOp);

		CachedSCCProvider->Execute(EditCLOp, ChangelistPtr, Concurrency, FSourceControlOperationComplete::CreateLambda(
			[this, InCallback](const FSourceControlOperationRef& UpdateOperation, ECommandResult::Type Result) {
				if(Result == ECommandResult::Failed)
				{
					UE_LOG(LogSubmitToolP4, Error, TEXT("CL %s edit changelist operation %s failed:"), *GetCLID(), *UpdateOperation->GetName().ToString());
					PrintErrorMessages(UpdateOperation->GetResultInfo());
				}
				else if(Result == ECommandResult::Succeeded)
				{
					UE_LOG(LogSubmitToolP4, Log, TEXT("CL %s edit changelist operation succeeded."), *GetCLID());
					OriginalDescription = FText::FromString(CLDescription);
				}
				else if(Result == ECommandResult::Cancelled)
				{
					UE_LOG(LogSubmitToolP4, Warning, TEXT("CL %s edit changelist operation cancelled."), *GetCLID());
				}

				ActiveP4Operations.Remove(UpdateOperation);

				InCallback.ExecuteIfBound();
			}
		));
	}
	else
	{
		InCallback.ExecuteIfBound();
	}
}

void FChangelistService::DeleteShelvedFiles(const FSourceControlOperationComplete& OnDeleteComplete)
{
	UE_LOG(LogSubmitToolP4, Log, TEXT("Removing shelved files in CL %s..."), *GetCLID());
	const TSharedRef<FDeleteShelved, ESPMode::ThreadSafe> DeleteShelvedOp = ISourceControlOperation::Create<FDeleteShelved>();

	ActiveP4Operations.Add(DeleteShelvedOp);

	CachedSCCProvider->Execute(DeleteShelvedOp, ChangelistPtr, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[this, OnDeleteComplete](const FSourceControlOperationRef& DeleteShelvedOp, ECommandResult::Type Result) {
			if(Result == ECommandResult::Failed)
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("CL %s delete shelved operation failed:"), *GetCLID());

				PrintErrorMessages(DeleteShelvedOp->GetResultInfo());
			}
			else if(Result == ECommandResult::Succeeded)
			{
				ShelvedFilesInCL.Reset();
				ShelvedFilesDepotPaths.Reset();
				UE_LOG(LogSubmitToolP4, Log, TEXT("CL %s delete shelved operation succeeded"), *GetCLID());
			}
			else if(Result == ECommandResult::Cancelled)
			{
				UE_LOG(LogSubmitToolP4, Warning, TEXT("CL %s delete shelved operation cancelled."), *GetCLID());
			}

			ActiveP4Operations.Remove(DeleteShelvedOp);
			OnDeleteComplete.ExecuteIfBound(DeleteShelvedOp, Result);
		}
	));
}

void FChangelistService::CreateShelvedFiles(const FSourceControlOperationComplete& OnCreateComplete)
{
	UE_LOG(LogSubmitToolP4, Log, TEXT("Creating shelved files for CL %s..."), *GetCLID());
	const TSharedRef<FShelve, ESPMode::ThreadSafe> CreateShelvedOp = ISourceControlOperation::Create<FShelve>();

	ActiveP4Operations.Add(CreateShelvedOp);

	CachedSCCProvider->Execute(CreateShelvedOp, ChangelistPtr, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[this, OnCreateComplete](const FSourceControlOperationRef& CreateShelvedOp, ECommandResult::Type Result) {
			if(Result == ECommandResult::Failed)
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("CL %s create shelved operation failed:"), *GetCLID());

				PrintErrorMessages(CreateShelvedOp->GetResultInfo());
			}
			else if(Result == ECommandResult::Succeeded)
			{
				ShelvedFilesInCL = ChangelistState->GetShelvedFilesStates();
				ShelvedFilesDepotPaths.Reset(ShelvedFilesInCL.Num());
				UE_LOG(LogSubmitToolP4, Log, TEXT("CL %s create shelved operation succeeded"), *GetCLID());
			}
			else if(Result == ECommandResult::Cancelled)
			{
				UE_LOG(LogSubmitToolP4, Warning, TEXT("CL %s create shelved operation cancelled."), *GetCLID());
			}

			ActiveP4Operations.Remove(CreateShelvedOp);
			OnCreateComplete.ExecuteIfBound(CreateShelvedOp, Result);
		}
	));
}

bool FChangelistService::P4Tick(float DeltaTime)
{
	for(const TSharedRef<ISourceControlOperation>& operation : ActiveP4Operations)
	{
		UE_LOG(LogSubmitToolP4, Log, TEXT("%s operation still in progress: %s"), *operation->GetName().ToString(), *operation->GetInProgressString().ToString());
	}

	return true;
}


bool FChangelistService::IsP4OperationRunning(FName OperationName)
{
	if(OperationName == FName())
	{
		return ActiveP4Operations.Num() > 0;
	}

	for(const TSharedRef<ISourceControlOperation>& Operation : ActiveP4Operations)
	{
		if(Operation->GetName().IsEqual(OperationName, ENameCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

void FChangelistService::CancelP4Operations(FName OperationName)
{
	for(const TSharedRef<ISourceControlOperation>& Operation : ActiveP4Operations)
	{
		if(OperationName == FName() || Operation->GetName().IsEqual(OperationName, ENameCase::IgnoreCase))
		{
			if(CachedSCCProvider->CanCancelOperation(Operation))
			{
				CachedSCCProvider->CancelOperation(Operation);
				UE_LOG(LogSubmitToolP4, Warning, TEXT("P4 Operation %s cancelling requested"), *Operation->GetName().ToString())
			}
		}
	}
}

void FChangelistService::CreateCLFromDefaultCL()
{
	// If we try to open submit tool with the default cl: Update all CL Status -> create a new CL -> move files from default to the new CL -> regular flow
	UE_LOG(LogSubmitToolP4, Log, TEXT("Default changelist is not supported by Submit Tool, creating a new CL and moving files..."));

	TSharedRef<FUpdatePendingChangelistsStatus> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
	UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);
	UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);

	ActiveP4Operations.Add(UpdatePendingChangelistsOperation);

	CachedSCCProvider->Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
		[this](const FSourceControlOperationRef& Operation, ECommandResult::Type Result) {
			ActiveP4Operations.Remove(Operation);

			if(Result == ECommandResult::Failed)
			{
				UE_LOG(LogSubmitToolP4, Error, TEXT("Failed to fetch changelists from p4."));
				PrintErrorMessages(Operation->GetResultInfo());
			}
			else if(Result == ECommandResult::Succeeded)
			{

				TArray<FString> FilesInDefault;
				for(const FSourceControlChangelistPtr cl : CachedSCCProvider->GetChangelists(EStateCacheUsage::Use))
				{
					if(cl->IsDefault())
					{
						for(FSourceControlStateRef file : CachedSCCProvider->GetState(cl.ToSharedRef(), EStateCacheUsage::Use)->GetFilesStates())
						{
							FilesInDefault.Emplace(file->GetFilename());
						}
					}
				}

				TSharedRef<FNewChangelist, ESPMode::ThreadSafe> NewCLOp = ISourceControlOperation::Create<FNewChangelist>();

				FStringFormatNamedArguments FormatArgs = { 
					{ TEXT("FileCount") ,FilesInDefault.Num() }
				};

				NewCLOp->SetDescription(FText::FromString(FString::Format(*Parameters.NewChangelistMessage, FormatArgs)));

				ActiveP4Operations.Add(NewCLOp);

				CachedSCCProvider->Execute(NewCLOp, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
					[this, FilesInDefault](const FSourceControlOperationRef& Operation, ECommandResult::Type Result) {
						ActiveP4Operations.Remove(Operation);

						if(Result == ECommandResult::Failed)
						{
							UE_LOG(LogSubmitToolP4, Error, TEXT("Failed to create new changelist from default"));
							PrintErrorMessages(Operation->GetResultInfo());
						}
						else if(Result == ECommandResult::Succeeded)
						{
							TSharedRef<FNewChangelist> NewCLOp = StaticCastSharedRef<FNewChangelist>(Operation);
							ChangelistPtr = NewCLOp->GetNewChangelist();
							ChangelistState = CachedSCCProvider->GetState(ChangelistPtr.ToSharedRef(), EStateCacheUsage::Use);

							OriginalDescription = ChangelistState->GetDescriptionText();
							CLDescription = ChangelistState->GetDescriptionText().ToString();


							TSharedRef<FMoveToChangelist> MoveOp = ISourceControlOperation::Create<FMoveToChangelist>();

							ActiveP4Operations.Add(MoveOp);
							CachedSCCProvider->Execute(MoveOp, ChangelistPtr, FilesInDefault, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateLambda(
								[this, FileCount = FilesInDefault.Num()](const FSourceControlOperationRef& Operation, ECommandResult::Type Result) {
									ActiveP4Operations.Remove(Operation);
									if(Result == ECommandResult::Failed)
									{
										UE_LOG(LogSubmitToolP4, Error, TEXT("Move files to CL failed"));
										PrintErrorMessages(Operation->GetResultInfo());
									}
									else if(Result == ECommandResult::Succeeded)
									{
										FilesInCL = ChangelistState->GetFilesStates();
										FilesDepotPaths.Reset(FilesInCL.Num());
										ShelvedFilesInCL = ChangelistState->GetShelvedFilesStates();
										ShelvedFilesDepotPaths.Reset(ShelvedFilesInCL.Num());
										CLID = ChangelistPtr->GetIdentifier();
										FConfiguration::AddOrUpdateEntry(TEXT("$(CL)"), GetCLID());

										UE_LOG(LogSubmitToolP4, Log, TEXT("Created CL %s and moved with %d files from the default CL."), *ChangelistPtr->GetIdentifier(), FileCount);
										UE_LOG(LogSubmitToolDebug, Log, TEXT("ChangeListService CLID Updated to %s"), *GetCLID());
										UE_LOG(LogSubmitToolDebug, Log, TEXT("Configuration updated value $(CL) to %s"), *GetCLID());

										CLReadyCallback.ExecuteIfBound(ChangelistState.IsValid());
									}
									else if(Result == ECommandResult::Cancelled)
									{
										UE_LOG(LogSubmitToolP4, Warning, TEXT("Move files to CL cancelled."), *GetCLID());
									}
								}));
						}
						else if(Result == ECommandResult::Cancelled)
						{
							UE_LOG(LogSubmitToolP4, Warning, TEXT("Create new CL was cancelled."), *GetCLID());
						}
					}
				));
			}
			else if(Result == ECommandResult::Cancelled)
			{
				UE_LOG(LogSubmitToolP4, Warning, TEXT("CL %s edit changelist operation cancelled."), *GetCLID());
			}
		}));
}

TArray<FSourceControlChangelistStatePtr> FChangelistService::GetOtherChangelistsStates()
{
	TArray<FSourceControlChangelistStatePtr> Output;

	for(const FSourceControlChangelistRef& CL : CachedSCCProvider->GetChangelists(EStateCacheUsage::Use))
	{
		if(CL->GetIdentifier() != CLID)
		{
			FSourceControlChangelistPtr ClPtr = CL;
			Output.Add(CachedSCCProvider->GetState(ClPtr.ToSharedRef(), EStateCacheUsage::Use));
		}
	}

	return Output;
}

void FChangelistService::PrintFilesAndShelvedFiles()
{
	if(FilesInCL.Num() > 0)
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Files in CL:"));
		for(const FSourceControlStateRef& File : FilesInCL)
		{
			UE_LOG(LogSubmitToolP4Debug, Log, TEXT("\n%s"), *File->GetFilename());
		}
	}

	if(ShelvedFilesInCL.Num() > 0)
	{
		UE_LOG(LogSubmitToolP4Debug, Log, TEXT("Shelved Files in CL:"));
		for(const FSourceControlStateRef& File : ShelvedFilesInCL)
		{
			UE_LOG(LogSubmitToolP4Debug, Log, TEXT("\n%s"), *File->GetFilename());
		}
	}
}

const TArray<FString>& FChangelistService::GetFilesDepotPaths(bool bForce)
{
	FScopeLock Lock(&Mutex);
	if(bForce || (FilesDepotPaths.IsEmpty() && !FilesInCL.IsEmpty()))
	{
		UE::Tasks::TTask<bool> OpenedTask = SourceControlService->RunCommand(TEXT("opened"), { "-c", CLID }, FOnSCCCommandComplete::CreateLambda(
			[this](bool bSuccess, const FSCCRecordSet& InResultValues, const FSourceControlResultInfo& InResultsInfo)
			{
				if(bSuccess)
				{
					for(const TMap<FString, FString>& Record : InResultValues)
					{
						if(Record.Contains(TEXT("depotFile")))
						{
							FilesDepotPaths.Add(Record[TEXT("depotFile")]);
						}
						else
						{
							const FString Base = TEXT("depotFile");
							size_t i = 0;
							FString PathsKey = FString::Printf(TEXT("%s%d"), *Base, i);
							while(Record.Contains(PathsKey))
							{
								FilesDepotPaths.Add(Record[PathsKey]);
								++i;
								PathsKey = FString::Printf(TEXT("%s%d"), *Base, i);
							}
						}
					}
				}
			}));
		OpenedTask.Wait();
	}

	return FilesDepotPaths;
}

const TArray<FString>& FChangelistService::GetShelvedFilesDepotPaths(bool bForce)
{
	// Don't bother with the default CL
	if (CLID.Equals("default", ESearchCase::IgnoreCase))
	{
		return ShelvedFilesDepotPaths;
	}

	FScopeLock Lock(&Mutex);
	if(bForce || (ShelvedFilesDepotPaths.IsEmpty() && !ShelvedFilesInCL.IsEmpty()))
	{
		UE::Tasks::TTask<bool> DescribeTask = SourceControlService->RunCommand(TEXT("describe"), { "-S", CLID }, FOnSCCCommandComplete::CreateLambda(
			[this](bool bSuccess, const FSCCRecordSet& InResultValues, const FSourceControlResultInfo& InResultsInfo)
			{
				if(bSuccess)
				{
					for(const TMap<FString, FString>& Record : InResultValues)
					{
						if(Record.Contains(TEXT("depotFile")))
						{
							FilesDepotPaths.Add(Record[TEXT("depotFile")]);
						}
						else
						{
							const FString Base = TEXT("depotFile");
							size_t i = 0;
							FString PathsKey = FString::Printf(TEXT("%s%d"), *Base, i);
							while(Record.Contains(PathsKey))
							{
								ShelvedFilesDepotPaths.Add(Record[PathsKey]);
								++i;
								PathsKey = FString::Printf(TEXT("%s%d"), *Base, i);
							}
						}
					}
				}
			}));
		DescribeTask.Wait();
	}

	return ShelvedFilesDepotPaths;
}
