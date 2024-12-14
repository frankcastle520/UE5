// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectCompileRunnable.h"

#include "HAL/FileManager.h"
#include "MuCO/UnrealMutableModelDiskStreamer.h"
#include "MuCO/UnrealToMutableTextureConversionUtils.h"
#include "MuCO/CustomizableObject.h"
#include "MuR/Model.h"
#include "MuT/Compiler.h"
#include "MuT/ErrorLog.h"
#include "MuT/UnrealPixelFormatOverride.h"
#include "Serialization/MemoryWriter.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Trace/Trace.inl"
#include "UObject/SoftObjectPtr.h"
#include "Engine/AssetUserData.h"

#include "DerivedDataCacheInterface.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"

class ITargetPlatform;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

#define UE_MUTABLE_CORE_REGION	TEXT("Mutable Core")


TAutoConsoleVariable<bool> CVarMutableCompilerConcurrency(
	TEXT("mutable.ForceCompilerConcurrency"),
	true,
	TEXT("Force the use of multithreading when compiling CustomizableObjects both in editor and cook commandlets."),
	ECVF_Default);

TAutoConsoleVariable<bool> CVarMutableCompilerDiskCache(
	TEXT("mutable.ForceCompilerDiskCache"),
	false,
	TEXT("Force the use of disk cache to reduce memory usage when compiling CustomizableObjects both in editor and cook commandlets."),
	ECVF_Default);

TAutoConsoleVariable<bool> CVarMutableCompilerFastCompression(
	TEXT("mutable.ForceFastTextureCompression"),
	false,
	TEXT("Force the use of lower quality but faster compression during cook."),
	ECVF_Default);


FCustomizableObjectCompileRunnable::FCustomizableObjectCompileRunnable(mu::Ptr<mu::Node> Root)
	: MutableRoot(Root)
	, bThreadCompleted(false)
{
	PrepareUnrealCompression();
}


mu::Ptr<mu::Image> FCustomizableObjectCompileRunnable::LoadResourceReferenced(int32 ID)
{
	MUTABLE_CPUPROFILER_SCOPE(LoadResourceReferenced);

	mu::Ptr<mu::Image> Image;
	if (!ReferencedTextures.IsValidIndex(ID))
	{
		// The id is not valid for this CO
		check(false);
		return Image;
	}

	// Find the texture id
	FMutableSourceTextureData& TextureData = ReferencedTextures[ID];

	// In the editor the src data can be directly accessed
	Image = new mu::Image();
	int32 MipmapsToSkip = 0;
	EUnrealToMutableConversionError Error = ConvertTextureUnrealSourceToMutable(Image.get(), TextureData, MipmapsToSkip);

	if (Error != EUnrealToMutableConversionError::Success)
	{
		// This could happen in the editor, because some source textures may have changed while there was a background compilation.
		// We just show a warning and move on. This cannot happen during cooks, so it is fine.
		UE_LOG(LogMutable, Warning, TEXT("Failed to load some source texture data for texture ID [%d]. Some textures may be corrupted."), ID);
	}

	return Image;
}


uint32 FCustomizableObjectCompileRunnable::Run()
{
	TRACE_BEGIN_REGION(UE_MUTABLE_CORE_REGION);

	UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable::Run start."), FPlatformTime::Seconds());

	uint32 Result = 1;
	ErrorMsg = FString();

	// Translate CO compile options into mu::CompilerOptions
	mu::Ptr<mu::CompilerOptions> CompilerOptions = new mu::CompilerOptions();

	bool bUseConcurrency = !Options.bIsCooking;
	if (CVarMutableCompilerConcurrency->GetBool())
	{
		bUseConcurrency = true;
	}

	CompilerOptions->SetUseConcurrency(bUseConcurrency);

	bool bUseDiskCache = Options.bUseDiskCompilation;
	if (CVarMutableCompilerDiskCache->GetBool())
	{
		bUseDiskCache = true;
	}

	CompilerOptions->SetUseDiskCache(bUseDiskCache);

	if (Options.OptimizationLevel > 2)
	{
		UE_LOG(LogMutable, Log, TEXT("Mutable compile optimization level out of range. Clamping to maximum."));
		Options.OptimizationLevel = 2;
	}

	switch (Options.OptimizationLevel)
	{
	case 0:
		CompilerOptions->SetOptimisationEnabled(false);
		CompilerOptions->SetConstReductionEnabled(false);
		CompilerOptions->SetOptimisationMaxIteration(1);
		break;

	case 1:
		CompilerOptions->SetOptimisationEnabled(false);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(1);
		break;

	case 2:
		CompilerOptions->SetOptimisationEnabled(true);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(0);
		break;

	default:
		CompilerOptions->SetOptimisationEnabled(true);
		CompilerOptions->SetConstReductionEnabled(true);
		CompilerOptions->SetOptimisationMaxIteration(0);
		break;
	}

	// Texture compression override, if necessary
	bool bUseHighQualityCompression = (Options.TextureCompression == ECustomizableObjectTextureCompression::HighQuality);
	if (CVarMutableCompilerFastCompression->GetBool())
	{
		bUseHighQualityCompression = false;
	}

	if (bUseHighQualityCompression)
	{
		CompilerOptions->SetImagePixelFormatOverride( UnrealPixelFormatFunc );
	}

	CompilerOptions->SetReferencedResourceCallback([this](int32 ID, TSharedPtr<mu::Ptr<mu::Image>> ResolvedImage, bool bRunImmediatlyIfPossible)
		{
			UE::Tasks::FTask LaunchTask = UE::Tasks::Launch(TEXT("ConstantGeneratorLaunchTasks"),
				[ID,ResolvedImage,this]()
				{
					mu::Ptr<mu::Image> Result = LoadResourceReferenced(ID);
					*ResolvedImage = Result;
				},
				LowLevelTasks::ETaskPriority::BackgroundLow
			);

			return LaunchTask;
		}
	);

	const int32 MinResidentMips = UTexture::GetStaticMinTextureResidentMipCount();
	CompilerOptions->SetDataPackingStrategy( MinResidentMips, Options.EmbeddedDataBytesLimit, Options.PackagedDataBytesLimit );

	// We always compile for progressive image generation.
	CompilerOptions->SetEnableProgressiveImages(true);
	
	CompilerOptions->SetImageTiling(Options.ImageTiling);

	mu::Ptr<mu::Compiler> Compiler = new mu::Compiler(CompilerOptions);

	UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable Compile start."), FPlatformTime::Seconds());
	Model = Compiler->Compile(MutableRoot);

	// Dump all the log messages from the compiler
	mu::Ptr<const mu::ErrorLog> pLog = Compiler->GetLog();
	for (int i = 0; i < pLog->GetMessageCount(); ++i)
	{
		const FString& Message = pLog->GetMessageText(i);
		const mu::ErrorLogMessageType MessageType = pLog->GetMessageType(i);
		const mu::ErrorLogMessageAttachedDataView MessageAttachedData = pLog->GetMessageAttachedData(i);

		if (MessageType == mu::ELMT_WARNING || MessageType == mu::ELMT_ERROR)
		{
			const EMessageSeverity::Type Severity = MessageType == mu::ELMT_WARNING ? EMessageSeverity::Warning : EMessageSeverity::Error;
			const ELoggerSpamBin SpamBin = [&] {
				switch (pLog->GetMessageSpamBin(i)) {
				case mu::ErrorLogMessageSpamBin::ELMSB_UNKNOWN_TAG:
					return ELoggerSpamBin::TagsNotFound;
				case mu::ErrorLogMessageSpamBin::ELMSB_ALL:
				default:
					return ELoggerSpamBin::ShowAll;
			}
			}();

			if (MessageAttachedData.m_unassignedUVs && MessageAttachedData.m_unassignedUVsSize > 0) 
			{			
				TSharedPtr<FErrorAttachedData> ErrorAttachedData = MakeShared<FErrorAttachedData>();
				ErrorAttachedData->UnassignedUVs.Reset();
				ErrorAttachedData->UnassignedUVs.Append(MessageAttachedData.m_unassignedUVs, MessageAttachedData.m_unassignedUVsSize);
				const UObject* Context = static_cast<const UObject*>(pLog->GetMessageContext(i));
				ArrayErrors.Add(FError(Severity, FText::AsCultureInvariant(Message), ErrorAttachedData, Context, SpamBin));
			}
			else
			{
				// TODO: Review, and probably propagate the UObject type into the runtime.
				const UObject* Context = static_cast<const UObject*>(pLog->GetMessageContext(i));
				const UObject* Context2 = static_cast<const UObject*>(pLog->GetMessageContext2(i));
				ArrayErrors.Add(FError(Severity, FText::AsCultureInvariant(Message), Context, Context2, SpamBin));
			}
		}
	}

	Compiler = nullptr;

	bThreadCompleted = true;

	UE_LOG(LogMutable, Verbose, TEXT("PROFILE: [ %16.8f ] FCustomizableObjectCompileRunnable::Run end."), FPlatformTime::Seconds());

	CompilerOptions->LogStats();

	TRACE_END_REGION(UE_MUTABLE_CORE_REGION);

	return Result;
}


bool FCustomizableObjectCompileRunnable::IsCompleted() const
{
	return bThreadCompleted;
}


const TArray<FCustomizableObjectCompileRunnable::FError>& FCustomizableObjectCompileRunnable::GetArrayErrors() const
{
	return ArrayErrors;
}


void FCustomizableObjectCompileRunnable::Tick()
{
	MUTABLE_CPUPROFILER_SCOPE(FCustomizableObjectCompileRunnable::Tick);

	check(IsInGameThread());

	constexpr double MaxSecondsPerFrame = 0.4;

	double MaxTime = FPlatformTime::Seconds() + MaxSecondsPerFrame;

	FReferenceResourceRequest Request;
	while (PendingResourceReferenceRequests.Dequeue(Request))
	{
		*Request.ResolvedImage = LoadResourceReferenced(Request.ID);
		Request.CompletionEvent->Trigger();

		// Simple time limit enforcement to avoid blocking the game thread if there are many requests.
		double CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime >= MaxTime)
		{
			break;
		}
	}
}


FCustomizableObjectSaveDDRunnable::FCustomizableObjectSaveDDRunnable(const TSharedPtr<FCompilationRequest>& InRequest,
	TSharedPtr<mu::Model> InModel,
	FModelResources& InModelResources,
	TSharedPtr<FModelStreamableBulkData> InModelStreamables)
{
	MUTABLE_CPUPROFILER_SCOPE(FCustomizableObjectSaveDDRunnable::FCustomizableObjectSaveDDRunnable)

	Model = InModel;
	ModelStreamables = InModelStreamables;

	Options = InRequest->GetCompileOptions();

	DDCKey = InRequest->GetDerivedDataCacheKey();
	DefaultDDCPolicy = InRequest->GetDerivedDataCachePolicy();

	UCustomizableObject* CustomizableObject = InRequest->GetCustomizableObject();
	CustomizableObjectName = GetNameSafe(CustomizableObject);

	CustomizableObjectHeader.InternalVersion = CustomizableObject->GetPrivate()->CurrentSupportedVersion;
	CustomizableObjectHeader.VersionId = CustomizableObject->GetPrivate()->GetVersionId();

	// Cache ModelResources
	{
		FMemoryWriter64 MemoryWriter(PlatformData.ModelResourcesData);
		FObjectAndNameAsStringProxyArchive ObjectWriter(MemoryWriter, true);
		InModelResources.Serialize(ObjectWriter, Options.bIsCooking);
	}

	// Cache Morphs and Clothing. 
	{
		// Do a copy of the Morph and Clothing Data generated at compile time. Only needed when cooking.
		// Morphs
		static_assert(TCanBulkSerialize<FMorphTargetVertexData>::Value);
		TArray<FMorphTargetVertexData>& MorphVertexData = InModelResources.EditorOnlyMorphTargetReconstructionData;

		PlatformData.MorphData.SetNum(MorphVertexData.Num() * sizeof(FMorphTargetVertexData));
		FMemory::Memcpy(PlatformData.MorphData.GetData(), MorphVertexData.GetData(), PlatformData.MorphData.Num());

		// Cloth
		static_assert(TCanBulkSerialize<FCustomizableObjectMeshToMeshVertData>::Value);
		TArray<FCustomizableObjectMeshToMeshVertData>& ClothingVertexData = InModelResources.EditorOnlyClothingMeshToMeshVertData;

		PlatformData.ClothingData.SetNum(ClothingVertexData.Num() * sizeof(FCustomizableObjectMeshToMeshVertData));
		FMemory::Memcpy(PlatformData.ClothingData.GetData(), ClothingVertexData.GetData(), PlatformData.ClothingData.Num());

		if (Options.bIsCooking)
		{
			InModelResources.EditorOnlyMorphTargetReconstructionData.Empty();
			InModelResources.EditorOnlyClothingMeshToMeshVertData.Empty();
		}
	}

	if (!Options.bIsCooking)
	{
		// We will be saving all compilation data in two separate files, write CO Data
		FolderPath = CustomizableObject->GetPrivate()->GetCompiledDataFolderPath();
		CompileDataFullFileName = FolderPath + CustomizableObject->GetPrivate()->GetCompiledDataFileName(true, Options.TargetPlatform);
		StreamableDataFullFileName = FolderPath + CustomizableObject->GetPrivate()->GetCompiledDataFileName(false, Options.TargetPlatform);
	}
}

uint32 FCustomizableObjectSaveDDRunnable::Run()
{
	MUTABLE_CPUPROFILER_SCOPE(FCustomizableObjectSaveDDRunnable::Run)

	if (Model)
	{
		CachePlatfromData();

		bool bStoredSuccessfully = false;

		// TODO UE-222775: Allow using DDC in editor builds, not just for cooking.
		if (Options.bIsCooking && Options.bStoreCompiledDataInDDC && !DDCKey.Hash.IsZero())
		{
			StoreCachedPlatformDataInDDC(bStoredSuccessfully);
		}

		if (!Options.bIsCooking && !bStoredSuccessfully)
		{
			StoreCachedPlatformDataToDisk(bStoredSuccessfully);
		}
	}

	bThreadCompleted = true;

	return 1;
}


bool FCustomizableObjectSaveDDRunnable::IsCompleted() const
{
	return bThreadCompleted;
}


const ITargetPlatform* FCustomizableObjectSaveDDRunnable::GetTargetPlatform() const
{
	return Options.TargetPlatform;
}


void FCustomizableObjectSaveDDRunnable::CachePlatfromData()
{
	MUTABLE_CPUPROFILER_SCOPE(CachePlatfromData);

	if (!Model || !ModelStreamables)
	{
		check(false);
		return;
	}

	// Cache ModelStreamalbes
	{
		PlatformData.ModelStreamables = ModelStreamables;

		// Generate list of files and update streamable blocks ids and offsets
		if (Options.bUseBulkData)
		{
			MutablePrivate::GenerateBulkDataFilesListWithFileLimit(Model, *PlatformData.ModelStreamables.Get(), MAX_uint8, PlatformData.BulkDataFiles);
		}
		else
		{
			const uint64 PackageDataBytesLimit = Options.bIsCooking ? Options.PackagedDataBytesLimit : MAX_uint64;
			MutablePrivate::GenerateBulkDataFilesListWithSizeLimit(Model, *PlatformData.ModelStreamables.Get(), Options.TargetPlatform, PackageDataBytesLimit, PlatformData.BulkDataFiles);
		}
	}


	// Cache Model and Model Roms
	{
		FMemoryWriter64 ModelMemoryWriter(PlatformData.ModelData);
		FUnrealMutableModelBulkWriterCook Streamer(&ModelMemoryWriter, &PlatformData.ModelStreamableData);

		// Serialize mu::Model and streamable resources 
		constexpr bool bDropData = true;
		mu::Model::Serialise(Model.Get(), Streamer, bDropData);
	}
}


void FCustomizableObjectSaveDDRunnable::StoreCachedPlatformDataInDDC(bool& bStoredSuccessfully)
{
	MUTABLE_CPUPROFILER_SCOPE(StoreCachedPlatformDataInDDC);

	using namespace UE::DerivedData;

	check(Model.Get() != nullptr);
	check(DDCKey.Hash.IsZero() == false);

	bStoredSuccessfully = false;

	// DDC record
	FCacheRecordBuilder RecordBuilder(DDCKey);

	// Store streamable resources info as FValues
	{
		MUTABLE_CPUPROFILER_SCOPE(SerializeModelStreamables);

		// ModelStreamable  will be modified for the DDC record. Modify a copy
		FModelStreamableBulkData ModelStreamablesDDC = *ModelStreamables.Get();

		// Generate list of files and update streamable blocks ids and offsets
		MutablePrivate::GenerateBulkDataFilesListWithFileLimit(Model, ModelStreamablesDDC, MAX_int16, BulkDataFilesDDC);

		TArray64<uint8> ModelStreamablesBytesDDC;
		FMemoryWriter64 MemoryWriterDDC(ModelStreamablesBytesDDC);
		MemoryWriterDDC << ModelStreamablesDDC;

		const FValue ModelStreamablesValue = FValue::Compress(FSharedBuffer::MakeView(ModelStreamablesBytesDDC.GetData(), ModelStreamablesBytesDDC.Num()));
		RecordBuilder.AddValue(MutablePrivate::GetDerivedDataModelStreamableBulkDataId(), ModelStreamablesValue);
	}

	// Store streamable resources as FValues
	{
		MUTABLE_CPUPROFILER_SCOPE(SerializeBulkDataForDDC);
	
		{
			MutablePrivate::FFile File;
			check(sizeof(FValueId::ByteArray) >= sizeof(File.DataType) + sizeof(File.Id) + sizeof(File.ResourceType) + sizeof(File.Flags));
		}

		FValueId::ByteArray ValueIdBytes = {};
		const auto WriteBulkDataDDC = [&RecordBuilder, &ValueIdBytes](MutablePrivate::FFile& File, TArray64<uint8>& FileBulkData, uint32 FileIndex)
			{
				int8 ValueIdOffset = 0;
				FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.DataType, sizeof(File.DataType));
				ValueIdOffset += sizeof(File.DataType);
				FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.Id, sizeof(File.Id));
				ValueIdOffset += sizeof(File.Id);
				FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.ResourceType, sizeof(File.ResourceType));
				ValueIdOffset += sizeof(File.ResourceType);
				FMemory::Memcpy(&ValueIdBytes[ValueIdOffset], &File.Flags, sizeof(File.Flags));

				const FValueId ValueId(ValueIdBytes);
				const FValue Value = FValue::Compress(FSharedBuffer::MakeView(FileBulkData.GetData(), FileBulkData.Num()));

				RecordBuilder.AddValue(ValueId, Value);
			};

		constexpr bool bDropData = false;
		MutablePrivate::SerializeBulkDataFiles(PlatformData, BulkDataFilesDDC, WriteBulkDataDDC, bDropData);
	}


	// Store BulkData Files as a FValue to reconstruct the data later on
	{
		MUTABLE_CPUPROFILER_SCOPE(SerializeBulkDataFilesForDDC);

		TArray<uint8> BulkDataFilesBytes;
		FMemoryWriter MemoryWriter(BulkDataFilesBytes);

		MemoryWriter << BulkDataFilesDDC;

		const FValue BulkDataFilesValue = FValue::Compress(FSharedBuffer::MakeView(BulkDataFilesBytes.GetData(), BulkDataFilesBytes.Num()));
		RecordBuilder.AddValue(MutablePrivate::GetDerivedDataBulkDataFilesId(), BulkDataFilesValue);
	}

	// Store ModelResources bytes as a FValue
	{
		MUTABLE_CPUPROFILER_SCOPE(SerializeModelResourcesForDDC);

		const FValue ModelResourcesValue = FValue::Compress(FSharedBuffer::MakeView(PlatformData.ModelResourcesData.GetData(), PlatformData.ModelResourcesData.Num()));
		RecordBuilder.AddValue(MutablePrivate::GetDerivedDataModelResourcesId(), ModelResourcesValue);
	}

	// Store Model bytes as a FValue
	{
		MUTABLE_CPUPROFILER_SCOPE(SerializeModelForDDC);

		const FValue ModelValue = FValue::Compress(FSharedBuffer::MakeView(PlatformData.ModelData.GetData(), PlatformData.ModelData.Num()));
		RecordBuilder.AddValue(MutablePrivate::GetDerivedDataModelId(), ModelValue);
	}

	// Push record to the DDC
	{
		MUTABLE_CPUPROFILER_SCOPE(PushRecordToDDC);

		FRequestOwner RequestOwner(UE::DerivedData::EPriority::Blocking);
		const FCachePutRequest PutRequest = { UE::FSharedString(CustomizableObjectName), RecordBuilder.Build(), DefaultDDCPolicy };
		GetCache().Put(MakeArrayView(&PutRequest, 1), RequestOwner,
			[&bStoredSuccessfully](FCachePutResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					bStoredSuccessfully = true;
				}
			});

		RequestOwner.Wait();
	}
}


void FCustomizableObjectSaveDDRunnable::StoreCachedPlatformDataToDisk(bool& bStoredSuccessfully)
{
	MUTABLE_CPUPROFILER_SCOPE(StoreCachedPlatformDataToDisk);

	check(Model.Get() != nullptr);
	check(!Options.bIsCooking);

	bStoredSuccessfully = false;

	// Create folder...
	IFileManager& FileManager = IFileManager::Get();
	FileManager.MakeDirectory(*FolderPath, true);

	// Delete files...
	
	
	bool bFilesDeleted = true;
	if (FileManager.FileExists(*CompileDataFullFileName)
		&& !FileManager.Delete(*CompileDataFullFileName, true, false, true))
	{
		UE_LOG(LogMutable, Error, TEXT("Failed to delete compiled data in file [%s]."), *CompileDataFullFileName);
		bFilesDeleted = false;
	}

	if (FileManager.FileExists(*StreamableDataFullFileName)
		&& !FileManager.Delete(*StreamableDataFullFileName, true, false, true))
	{
		UE_LOG(LogMutable, Error, TEXT("Failed to delete streamed data in file [%s]."), *StreamableDataFullFileName);
		bFilesDeleted = false;
	}

	if (!bFilesDeleted)
	{
		// Couldn't delete files. Delete model and return.
		Model.Reset();
		return;
	}

	// Serialize Streamable resources
	{
		// Create file writer
		TUniquePtr<FArchive> StreamableMemoryWriter(FileManager.CreateFileWriter(*StreamableDataFullFileName));
		check(StreamableMemoryWriter);

		// Serailize headers to validate data
		*StreamableMemoryWriter << CustomizableObjectHeader;

		const auto WriteBulkDataToDisk = [&StreamableMemoryWriter](MutablePrivate::FFile& File, TArray64<uint8>& FileBulkData, uint32 FileIndex)
			{
				switch (File.DataType)
				{
				case MutablePrivate::EDataType::Model:
				{
					StreamableMemoryWriter->Serialize(FileBulkData.GetData(), FileBulkData.Num() * sizeof(uint8));
					break;
				}
				case MutablePrivate::EDataType::RealTimeMorph:
				{
					// TODO: Store clothing streamable to disk. UE-222777
					break;
				}
				case MutablePrivate::EDataType::Clothing:
				{
					// TODO: Store clothing streamable to disk. UE-222777
					break;
				}
				default:
					break;
				}
				
			};

		// Serialize streamable resources into a single file and fix offsets
		constexpr bool bDropData = true;
		MutablePrivate::SerializeBulkDataFiles(PlatformData, PlatformData.BulkDataFiles, WriteBulkDataToDisk, bDropData);
		StreamableMemoryWriter->Flush();
		StreamableMemoryWriter->Close();

		PlatformData.MorphData.Empty();
		PlatformData.ClothingData.Empty();
	}

	// Serialize Model and ModelResources. Store after SerializeBulkDataFiles fixes the HashToStreamableFiles offsets.
	{
		// Create file writer
		TUniquePtr<FArchive> ModelMemoryWriter(FileManager.CreateFileWriter(*CompileDataFullFileName));
		check(ModelMemoryWriter);

		// Serailize headers to validate data
		*ModelMemoryWriter << CustomizableObjectHeader;

		ModelMemoryWriter->Serialize(PlatformData.ModelResourcesData.GetData(), PlatformData.ModelResourcesData.Num() * sizeof(uint8));

		{
			// ModelMemoryWriter (Writer to disk) doesn't handle FNames properly. Serialize them ModelStreamables in two steps.
			TArray64<uint8> ModelStreamablesBytes;
			FMemoryWriter64 ModelStreamablesMemoryWriter(ModelStreamablesBytes);
			ModelStreamablesMemoryWriter << *PlatformData.ModelStreamables.Get();
			ModelMemoryWriter->Serialize(ModelStreamablesBytes.GetData(), ModelStreamablesBytes.Num() * sizeof(uint8));
		}

		ModelMemoryWriter->Serialize(PlatformData.ModelData.GetData(), PlatformData.ModelData.Num() * sizeof(uint8));
		
		ModelMemoryWriter->Flush();
		ModelMemoryWriter->Close();

		PlatformData.ModelData.Empty();
	}

	bStoredSuccessfully = true;
}


#undef LOCTEXT_NAMESPACE

