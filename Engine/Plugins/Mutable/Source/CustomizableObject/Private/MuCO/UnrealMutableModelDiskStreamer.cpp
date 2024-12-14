// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/UnrealMutableModelDiskStreamer.h"

#include "Async/AsyncFileHandle.h"
#include "HAL/PlatformFile.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuR/Model.h"
#include "MuR/MutableTrace.h"
#include "MuCO/LogBenchmarkUtil.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/BulkData.h"

#if WITH_EDITOR
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#endif

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Streaming Ops"), STAT_MutableStreamingOps, STATGROUP_Mutable);


//-------------------------------------------------------------------------------------------------
#if WITH_EDITOR

UnrealMutableOutputStream::UnrealMutableOutputStream(FArchive& ar)
	: m_ar(ar)
{
}


void UnrealMutableOutputStream::Write(const void* pData, uint64 size)
{
	// TODO: ugly const cast
	m_ar.Serialize(const_cast<void*>(pData), size);
}

#endif

//-------------------------------------------------------------------------------------------------
UnrealMutableInputStream::UnrealMutableInputStream(FArchive& ar)
	: m_ar(ar)
{
}


void UnrealMutableInputStream::Read(void* pData, uint64 size)
{
	m_ar.Serialize(pData, size);
}


//-------------------------------------------------------------------------------------------------
FUnrealMutableModelBulkReader::~FUnrealMutableModelBulkReader()
{
}


bool FUnrealMutableModelBulkReader::PrepareStreamingForObject(UCustomizableObject* CustomizableObject)
{
	if (!CustomizableObject)
	{
		check(false);
		return false;
	}

	// See if we can free previuously allocated resources
	for (int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); )
	{
		// Close open file handles
		Objects[ObjectIndex].ReadFileHandles.Empty();

		if (!Objects[ObjectIndex].Model.Pin() && Objects[ObjectIndex].CurrentReadRequests.IsEmpty())
		{
			Objects.RemoveAtSwap(ObjectIndex);
		}
		else
		{
			++ObjectIndex;
		}
	}

	// Is the object already prepared for streaming?
	bool bAlreadyStreaming = Objects.FindByPredicate(
		[CustomizableObject](const FObjectData& d)
		{ return d.Model.Pin().Get() == CustomizableObject->GetPrivate()->GetModel().Get(); })
		!=
		nullptr;

	const FModelResources& ModelResources = CustomizableObject->GetPrivate()->GetModelResources();

	if (!bAlreadyStreaming)
	{
		FObjectData NewData;
		NewData.Model = TWeakPtr<const mu::Model>(CustomizableObject->GetPrivate()->GetModel());
		NewData.ModelStreamableBulkData = CustomizableObject->GetPrivate()->GetModelStreamableBulkData();

		if (!NewData.ModelStreamableBulkData || NewData.ModelStreamableBulkData->ModelStreamables.IsEmpty())
		{
			UE_LOG(LogMutable, Warning, TEXT("Streaming: Customizable Object %s has no data to stream."), *CustomizableObject->GetName());

#if !WITH_EDITOR
			check(false);
#endif
			return false;
		}

#if WITH_EDITOR
		FString FolderPath = CustomizableObject->GetPrivate()->GetCompiledDataFolderPath();
		FString FullFileName = FolderPath + CustomizableObject->GetPrivate()->GetCompiledDataFileName(false, nullptr, true);
		NewData.BulkFilePrefix = *FullFileName;

		NewData.bIsStoredInDDC = ModelResources.bIsStoredInDDC;
		NewData.DDCKey = ModelResources.DDCKey;
		NewData.DDCPolicy = ModelResources.DDCDefaultPolicy;
#else
		if (NewData.ModelStreamableBulkData->StreamableBulkData.IsEmpty())
		{
			const UCustomizableObjectBulk* BulkData = CustomizableObject->GetPrivate()->GetStreamableBulkData();
			if (!BulkData)
			{
				UE_LOG(LogMutable, Warning, TEXT("Streaming: Customizable Object %s is missing the BulkData export."), *CustomizableObject->GetName());
				return false;
			}

			NewData.BulkFilePrefix = BulkData->GetBulkFilePrefix();
		}
#endif

		Objects.Add(MoveTemp(NewData));
	}

	return true;
}


#if WITH_EDITOR
void FUnrealMutableModelBulkReader::CancelStreamingForObject(const UCustomizableObject* CustomizableObject)
{
	if (!CustomizableObject)
	{
		check(false);
	}

	// See if we can free previuously allocated resources
	for (int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ++ObjectIndex)
	{
		if (Objects[ObjectIndex].Model.Pin() == CustomizableObject->GetPrivate()->GetModel())
		{
			check(Objects[ObjectIndex].CurrentReadRequests.IsEmpty());

			Objects.RemoveAtSwap(ObjectIndex);
			break;
		}
	}
}


bool FUnrealMutableModelBulkReader::AreTherePendingStreamingOperationsForObject(const UCustomizableObject* CustomizableObject) const
{
	// This happens in the game thread
	check(IsInGameThread());

	if (!CustomizableObject)
	{
		check(false);
		return false;
	}

	for (int32 ObjectIndex = 0; ObjectIndex < Objects.Num(); ++ObjectIndex)
	{
		if (Objects[ObjectIndex].Model.Pin() == CustomizableObject->GetPrivate()->GetModel())
		{
			if (!Objects[ObjectIndex].CurrentReadRequests.IsEmpty())
			{
				return true;
			}
		}
	}

	return false;
}

#endif // WITH_EDITOR


void FUnrealMutableModelBulkReader::EndStreaming()
{
	for (FObjectData& o : Objects)
	{
		for (TPair<OPERATION_ID, FReadRequest>& it : o.CurrentReadRequests)
		{
			if (it.Value.FileReadRequest)
			{
				it.Value.FileReadRequest->WaitCompletion();
			}
			else if (it.Value.BulkReadRequest)
			{
				it.Value.BulkReadRequest->WaitCompletion();
			}
		}
	}
	Objects.Empty();
}


static int32 StreamPriority = 4;
FAutoConsoleVariableRef CVarStreamPriority(
	TEXT("Mutable.StreamPriority"),
	StreamPriority,
	TEXT(""));


mu::ModelReader::OPERATION_ID FUnrealMutableModelBulkReader::BeginReadBlock(const mu::Model* Model, uint32 Key, void* pBuffer, uint64 size, TFunction<void(bool bSuccess)>* CompletionCallback)
{
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableModelBulkStreamer::OpenReadFile);

	UE_LOG(LogMutable, VeryVerbose, TEXT("Streaming: reading data %08llu."), Key);

	// Find the object we are streaming for
	FObjectData* ObjectData = Objects.FindByPredicate(
		[Model](const FObjectData& d) {return d.Model.Pin().Get() == Model; });

	if (!ObjectData)
	{
		// The object has been unloaded. Streaming is not possible. 
		// This may happen in the editor if we are recompiling an object but we still have instances from the old
		// object that have progressive mip generation.
		if (CompletionCallback)
		{
			(*CompletionCallback)(false);
		}
		return -1;
	}

	const FMutableStreamableBlock* Block = ObjectData->ModelStreamableBulkData->ModelStreamables.Find(Key);
	if (!Block)
	{
		// File Handle not found! This shouldn't really happen.
		UE_LOG(LogMutable, Error, TEXT("Streaming Block not found!"));
		check(false);

		if (CompletionCallback)
		{
			(*CompletionCallback)(false);
		}
		return -1;
	}

	OPERATION_ID Result = ++LastOperationID;

	if(ObjectData->ModelStreamableBulkData->StreamableBulkData.IsValidIndex(Block->FileId))
	{
		FByteBulkData& BulkData = ObjectData->ModelStreamableBulkData->StreamableBulkData[Block->FileId];

		FBulkDataIORequestCallBack IOCallback;

		if (CompletionCallback)
		{
			IOCallback = [CompletionCallbackCapture = *CompletionCallback](bool bWasCancelled, IBulkDataIORequest*)
				{
					CompletionCallbackCapture(!bWasCancelled);
				};
		}

		FReadRequest& Request = ObjectData->CurrentReadRequests.Add(Result);

		Request.BulkReadRequest = MakeShareable(BulkData.CreateStreamingRequest(
			Block->Offset,
			size,
			(EAsyncIOPriorityAndFlags)StreamPriority,
			&IOCallback,
			reinterpret_cast<uint8*>(pBuffer)));
	}
	else
	{
#if WITH_EDITOR
		if (ObjectData->bIsStoredInDDC)
		{
			MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableModelBulkStreamer::OpenReadFileDDC);

			using namespace UE::DerivedData;

			FValueId::ByteArray ValueIdBytes = {};
			check(sizeof(FValueId::ByteArray) >= 4 + sizeof(Block->FileId));

			MutablePrivate::EDataType DataType = MutablePrivate::EDataType::Model;
			FMemory::Memcpy(&ValueIdBytes, &DataType, sizeof(DataType));
			FMemory::Memcpy(&ValueIdBytes[4], &Block->FileId, sizeof(Block->FileId));
			FValueId ResourceId = FValueId(ValueIdBytes);

			// Skip loading values by default
			FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::Default | ECachePolicy::SkipData);

			PolicyBuilder.AddValuePolicy(ResourceId, ECachePolicy::Default); // Override policy for the Model. Always load the compiled model data.

			FCacheGetRequest Request;
			Request.Name = ObjectData->BulkFilePrefix;
			Request.Key = ObjectData->DDCKey;
			Request.Policy = PolicyBuilder.Build();

			FReadRequest ReadRequest;
			ReadRequest.DDCReadRequest = MakeShared<FRequestOwner>(EPriority::High);

			GetCache().Get(MakeArrayView(&Request, 1), *ReadRequest.DDCReadRequest.Get(),
				[CompletionCallbackCapture = *CompletionCallback, Block, ResourceId, pBuffer, size](FCacheGetResponse&& Response)
				{
					bool bSuccess = Response.Status == EStatus::Ok;
					if (bSuccess)
					{
						const FCompressedBuffer& CompressedBuffer = Response.Record.GetValue(ResourceId).GetData();
						if (size < CompressedBuffer.GetRawSize())
						{
							check(CompressedBuffer.GetRawSize() >= Block->Offset + size);
							FSharedBuffer DecompressedBuffer = CompressedBuffer.Decompress();
							FMemory::Memcpy(pBuffer, reinterpret_cast<const uint8*>(DecompressedBuffer.GetData()) + Block->Offset, size);
						}
						else
						{
							check(CompressedBuffer.TryDecompressTo(MakeMemoryView(pBuffer, size)));
						}
					}
					else
					{
						check(false);
					}

					CompletionCallbackCapture(bSuccess);
				});

			ObjectData->CurrentReadRequests.Add(Result, ReadRequest);
		}
		else
#endif
		{

			int32 BulkDataOffsetInFile = 0;

#if WITH_EDITOR
			BulkDataOffsetInFile = sizeof(MutableCompiledDataStreamHeader);
#endif

			FReadRequest ReadRequest;

			if (CompletionCallback)
			{
				ReadRequest.FileCallback = MakeShared<TFunction<void(bool, IAsyncReadRequest*)>>([CompletionCallbackCapture = *CompletionCallback](bool bWasCancelled, IAsyncReadRequest*) -> void
					{
						CompletionCallbackCapture(!bWasCancelled);
					});
			}

			TSharedPtr<IAsyncReadFileHandle> FileHandle;
			{
				FScopeLock Lock(&FileHandlesCritical);

				TSharedPtr<IAsyncReadFileHandle>& Found = ObjectData->ReadFileHandles.FindOrAdd(Block->FileId);
				if (!Found)
				{
#if WITH_EDITOR
					FString FilePath = ObjectData->BulkFilePrefix;
#else
					FString FilePath = FString::Printf(TEXT("%s-%08x.mut"), *ObjectData->BulkFilePrefix, Block->FileId);
					if (Block->Flags == uint32(mu::ERomFlags::HighRes))
					{
						FilePath += TEXT(".high");
					}
#endif

					Found = MakeShareable(FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*FilePath));

					if (!Found)
					{
						UE_LOG(LogMutable, Error, TEXT("Failed to create AsyncReadFileHandle. File Path [%s]."), *FilePath);
						check(false);
						return -1;
					}
				}

				FileHandle = Found;
			}

			ReadRequest.FileReadRequest = MakeShareable(FileHandle->ReadRequest(
				BulkDataOffsetInFile + Block->Offset,
				size,
				(EAsyncIOPriorityAndFlags)StreamPriority,
				ReadRequest.FileCallback.Get(),
				reinterpret_cast<uint8*>(pBuffer)));

			ObjectData->CurrentReadRequests.Add(Result, ReadRequest);
		}
	}

	INC_DWORD_STAT(STAT_MutableStreamingOps);

	return Result;
}


bool FUnrealMutableModelBulkReader::IsReadCompleted(mu::ModelReader::OPERATION_ID OperationId)
{
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableModelBulkStreamer::IsReadCompleted);

	for (FObjectData& o : Objects)
	{
		if (FReadRequest* ReadRequest = o.CurrentReadRequests.Find(OperationId))
		{
			if (ReadRequest->FileReadRequest)
			{
				return ReadRequest->FileReadRequest->PollCompletion();
			}
			else if (ReadRequest->BulkReadRequest)
			{
				return ReadRequest->BulkReadRequest->PollCompletion();
			}
#if WITH_EDITORONLY_DATA
			else if (ReadRequest->DDCReadRequest)
			{
				return ReadRequest->DDCReadRequest->Poll(); // ReadLock
			}
#endif
		}
	}

	UE_LOG(LogMutable, Error, TEXT("Operation not found in IsReadCompleted."));
	check(false);
	return true;
}


bool FUnrealMutableModelBulkReader::EndRead(mu::ModelReader::OPERATION_ID OperationId)
{
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableModelBulkStreamer::EndRead);

	bool bSuccess = true;
	bool bFound = false;
	for (FObjectData& o : Objects)
	{
		FReadRequest* ReadRequest = o.CurrentReadRequests.Find(OperationId);
		if (ReadRequest)
		{
			bool bCompleted = false;
			bool bResult = false;

			if (ReadRequest->FileReadRequest)
			{
				bCompleted = ReadRequest->FileReadRequest->WaitCompletion();
				bResult = ReadRequest->FileReadRequest->GetReadResults() != nullptr;
			}
			else if (ReadRequest->BulkReadRequest)
			{
				bCompleted = ReadRequest->BulkReadRequest->WaitCompletion();
				bResult = ReadRequest->BulkReadRequest->GetReadResults() != nullptr;
			}
#if WITH_EDITORONLY_DATA
			else if (ReadRequest->DDCReadRequest)
			{
				ReadRequest->DDCReadRequest->Wait();
				
				// bSuccess TODO Pere: find a way to know if the request completed successfully
				bCompleted = true;
				bResult = true;
			}
#endif
			if (!bCompleted)
			{
				UE_LOG(LogMutable, Error, TEXT("Operation failed to complete in EndRead."));
				check(false);
			}

			bSuccess = bCompleted && bResult;
			
			o.CurrentReadRequests.Remove(OperationId);
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		UE_LOG(LogMutable, Error, TEXT("Operation not found in EndRead."));
		check(false);
	}

	return bSuccess;
}


#if WITH_EDITOR

//-------------------------------------------------------------------------------------------------
FUnrealMutableModelBulkWriterEditor::FUnrealMutableModelBulkWriterEditor(FArchive* InMainDataArchive, FArchive* InStreamedDataArchive)
{
	MainDataArchive = InMainDataArchive;
	StreamedDataArchive = InStreamedDataArchive;
	CurrentWriteFile = nullptr;
}


void FUnrealMutableModelBulkWriterEditor::OpenWriteFile(uint32 BlockKey)
{
	if (BlockKey == 0) // Model
	{
		check(MainDataArchive);
		CurrentWriteFile = MainDataArchive;
	}
	else
	{
		check(StreamedDataArchive);
		CurrentWriteFile = StreamedDataArchive;
	}
}


void FUnrealMutableModelBulkWriterEditor::Write(const void* pBuffer, uint64 size)
{
	check(CurrentWriteFile);
	CurrentWriteFile->Serialize(const_cast<void*>(pBuffer), size);
}


void FUnrealMutableModelBulkWriterEditor::CloseWriteFile()
{
	CurrentWriteFile = nullptr;
}



//-------------------------------------------------------------------------------------------------
FUnrealMutableModelBulkWriterCook::FUnrealMutableModelBulkWriterCook(FArchive* InMainDataArchive, MutablePrivate::FModelStreamableData* InStreamedData)
{
	MainDataArchive = InMainDataArchive;
	StreamedData = InStreamedData;
	CurrentKey = 0;
}


void FUnrealMutableModelBulkWriterCook::OpenWriteFile(uint32 BlockKey)
{
	CurrentKey = BlockKey;
}


void FUnrealMutableModelBulkWriterCook::Write(const void* pBuffer, uint64 size)
{
	if (CurrentKey == 0)
	{
		MainDataArchive->Serialize(const_cast<void*>(pBuffer), size);
	}
	else
	{
		StreamedData->Set(CurrentKey, reinterpret_cast<const uint8*>(pBuffer), size);
	}
}


void FUnrealMutableModelBulkWriterCook::CloseWriteFile()
{
}


#endif

