// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkUAssetRecording.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "LiveLinkHubClient.h"
#include "LiveLinkHubLog.h"
#include "LiveLinkHubModule.h"
#include "LiveLinkUAssetRecordingPlayer.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Recording/LiveLinkHubPlaybackController.h"
#include "Recording/LiveLinkRecordingRangeHelpers.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Package.h"

#define LIVELINKHUB_FRAME_DEBUG 0

ULiveLinkUAssetRecording::~ULiveLinkUAssetRecording()
{
	if (!IsTemplate() && !IsEngineExitRequested())
	{
		UnloadRecordingData();
	}
}

void ULiveLinkUAssetRecording::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	AnimationData.Serialize(Ar, this);
}

void ULiveLinkUAssetRecording::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

	if (DuplicateMode == EDuplicateMode::Normal)
	{
		EjectAndUnload();
	}
}

void ULiveLinkUAssetRecording::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);

	EjectAndUnload();
}

FFrameRate ULiveLinkUAssetRecording::GetGlobalFrameRate() const
{
	return CalculateFrameRate(GetMaxFrames(), GetLastTimestamp());
}

void ULiveLinkUAssetRecording::SaveRecordingData()
{
	bIsSavingRecordingData = true;
	
	TArray64<uint8> Memory;
	FMemoryWriter64 Archive(Memory);
	
	int32 RecordingVersionToSave = RecordingVersion;
	Archive << RecordingVersionToSave;

	// How much static data to expect.
	int32 NumStaticData = RecordingData.StaticData.Num();
	Archive << NumStaticData;
		
	for (TTuple<FLiveLinkSubjectKey, FLiveLinkRecordingStaticDataContainer>& StaticData : RecordingData.StaticData)
	{
		SaveFrameData(&Archive, StaticData.Key, StaticData.Value);
	}
	
	// How much frame data to expect.
	int32 NumFrameData = RecordingData.FrameData.Num();
	Archive << NumFrameData;
		
	for (TTuple<FLiveLinkSubjectKey, FLiveLinkRecordingBaseDataContainer>& FrameData : RecordingData.FrameData)
	{
		SaveFrameData(&Archive, FrameData.Key, FrameData.Value);
	}

	AnimationData.WriteBulkData(Memory);

	bIsSavingRecordingData = false;
}

void ULiveLinkUAssetRecording::LoadRecordingData(int32 InInitialFrame, int32 InNumFramesToLoad)
{
	if (bIsFullyLoaded)
	{
		return;
	}
	
	bCancelStream = false;
	bPauseStream = false;
	OnStreamPausedEvent->Reset();
	OnStreamUnpausedEvent->Reset();
	
	int32 StartFrame = InInitialFrame - InNumFramesToLoad;
	if (StartFrame < 0)
	{
		StartFrame = 0;
	}
	
	// Additional buffer to each side, plus the initial frame.
	InNumFramesToLoad = (InNumFramesToLoad * 2) + 1;

	// Perform initial setup of the file reader.
	if (!AsyncStreamTask.IsValid())
	{
		FrameFileData.Empty();
	}

	// Stream more to the left if we're nearing the end of the recording.
	const int32 FramesPastLimit = FMath::Max(0, (StartFrame + InNumFramesToLoad) - GetMaxFrames());
	EarliestFrameToStream = StartFrame - FramesPastLimit;

	if (InitialFrameToStream != InInitialFrame)
	{
		// Signal to the async thread we're changing the desired frames, so it will restart itself using
		// the most up-to-date requested frame. 
		StreamingFrameChangeFromFrame = InitialFrameToStream;
		UE_LOG(LogLiveLinkHub, Verbose, TEXT("Stream initial frame changed from: %d to %d "), InInitialFrame, InitialFrameToStream);
	}
	
	InitialFrameToStream = InInitialFrame;
	TotalFramesToStream = InNumFramesToLoad;

	if (!OnPreGarbageCollectHandle.IsValid())
	{
		OnPreGarbageCollectHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &ULiveLinkUAssetRecording::OnPreGarbageCollect);
	}

	if (!OnPostGarbageCollectHandle.IsValid())
	{
		OnPostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &ULiveLinkUAssetRecording::OnPostGarbageCollect);
	}
	
	if (!AsyncStreamTask.IsValid())
	{
		AsyncStreamTask = MakeUnique<FAsyncTask<FLiveLinkStreamAsyncTask>>(this);
		AsyncStreamTask->StartBackgroundTask();
	}
}

void ULiveLinkUAssetRecording::UnloadRecordingData()
{
	// We need to prevent unloading if a package is being saved, but if this is called in a case where there is no outer,
	// the engine will CastCheck to find the package and fail. We need to avoid in this scenario since that would imply
	// no package is being saved. This was reported being triggered during an editor shutdown under certain conditions.
	const UPackage* Package = (GetOuter() == nullptr) ? nullptr : GetPackage();
	if (IsSavingRecordingData() || (Package && Package->HasAnyPackageFlags(PKG_IsSaving)))
	{
		UE_LOG(LogLiveLinkHub, Warning, TEXT("Attempted to unload %s while the package was still being saved"), *GetName());
		return;
	}
	
	bCancelStream = true;
	UnpauseStream();
	bIsFullyLoaded = false;

	if (AsyncStreamTask.IsValid())
	{
		if (!AsyncStreamTask->Cancel())
		{
			AsyncStreamTask->EnsureCompletion();
		}
		AsyncStreamTask.Reset();
	}

	if (OnPreGarbageCollectHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(OnPreGarbageCollectHandle);
		OnPreGarbageCollectHandle.Reset();
	}

	if (OnPostGarbageCollectHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(OnPostGarbageCollectHandle);
		OnPostGarbageCollectHandle.Reset();
	}

	bPerformedInitialLoad = false;

	AnimationData.UnloadBulkData();

	FrameFileData.Empty();
	RecordingMaxFrames = 0;
	RecordingLastTimestamp = 0.0;
	MaxFrameDiskSize = 0;
	EarliestFrameToStream = 0;
	InitialFrameToStream = 0;
	TotalFramesToStream = 0;
	
	for (TTuple<FLiveLinkSubjectKey, FLiveLinkRecordingStaticDataContainer>& StaticData : RecordingData.StaticData)
	{
		StaticData.Value.Timestamps.Empty();
		StaticData.Value.RecordedData.Empty();
	}

	for (TTuple<FLiveLinkSubjectKey, FLiveLinkRecordingBaseDataContainer>& FrameData : RecordingData.FrameData)
	{
		FrameData.Value.Timestamps.Empty();
		FrameData.Value.RecordedData.Empty();
	}
}

bool ULiveLinkUAssetRecording::WaitForBufferedFrames(int32 InMinFrame, int32 InMaxFrame)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::WaitForBufferedFrames"), STAT_ULiveLinkUAssetRecording_WaitForBufferedFrames, STATGROUP_LiveLinkHub);
	
	if (AsyncStreamTask.IsValid())
	{
		// Max frames isn't set until after the initial load.
		while (!bPerformedInitialLoad)
		{
			FPlatformProcess::Sleep(0.002);
		}

		const int32 MaxFrames = GetMaxFrames();
		if (MaxFrames <= 0)
		{
			return true;
		}
		
		// Clamp the frame range to the max possible range. If the selection range extends the actual frame range
		// then there would be nothing to load.
		const int32 EndFrame = MaxFrames - 1;
		InMinFrame = FMath::Clamp(InMinFrame, 0, EndFrame);
		InMaxFrame = FMath::Clamp(InMaxFrame, 0, EndFrame);

		const int32 InTotalFrames = InMaxFrame - InMinFrame + 1;
		const TRange<int32> InRange(InMinFrame, InMaxFrame);

		while (true)
		{
			if (InTotalFrames > TotalFramesToStream
			|| IsFrameRangeBuffered(InRange)
			|| AsyncStreamTask->IsDone())
			{
				break;
			}
			// We could potentially reduce blocking operations here by leveraging StreamingFrameChangeFromFrame and returning false
			// if it has changed. Doing so would require both allowing it to change at this point (this is all done from the game thread)
			// and another mechanism for ensuring the requested frame(s) we are blocking for are still delivered once loaded.
			
			FPlatformProcess::Sleep(0.002);
		}
	}

	return true;
}

UE::LiveLinkHub::RangeHelpers::Private::TRangeArray<int32> ULiveLinkUAssetRecording::GetBufferedFrameRanges() const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::GetBufferedFrameRanges"), STAT_ULiveLinkUAssetRecording_GetBufferedFrameRanges, STATGROUP_LiveLinkHub);
	
	if (bIsFullyLoaded)
	{
		return { UE::LiveLinkHub::RangeHelpers::Private::MakeInclusiveRange(0, RecordingMaxFrames - 1) };
	}

	FScopeLock Lock(&BufferedFrameMutex);
	return BufferedFrameRanges;
}

bool ULiveLinkUAssetRecording::IsFrameRangeBuffered(const TRange<int32>& InRange) const
{
	const UE::LiveLinkHub::RangeHelpers::Private::TRangeArray<int32> Ranges = GetBufferedFrameRanges();
	for (const TRange<int32>& Range : Ranges)
	{
		if (Range.Contains(InRange))
		{
			return true;
		}
	}
	return false;
}

void ULiveLinkUAssetRecording::CopyRecordingData(FLiveLinkPlaybackTracks& InOutLiveLinkPlaybackTracks) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::CopyRecordingData"), STAT_ULiveLinkUAssetRecording_CopyRecordingData, STATGROUP_LiveLinkHub);
	
	FScopeLock Lock(&DataContainerMutex);

	for (const TPair<FLiveLinkSubjectKey, FLiveLinkRecordingStaticDataContainer>& Pair : RecordingData.StaticData)
	{
		// Modify subject name so a duplicate FLiveLinkSubjectKey below doesn't produce the same hash. This allows us to efficiently
		// reuse tracks, as well as preserve the absolute frame index, which is needed since frame data is streamed in.
		FLiveLinkSubjectKey StaticSubjectKey = Pair.Key;
		StaticSubjectKey.SubjectName.Name = *(StaticSubjectKey.SubjectName.ToString() + "_STATIC");
		FLiveLinkPlaybackTrack& PlaybackTrack = InOutLiveLinkPlaybackTracks.Tracks.FindOrAdd(StaticSubjectKey);

		PlaybackTrack.FrameData = Pair.Value.RecordedData;
		PlaybackTrack.Timestamps = TConstArrayView<double>(Pair.Value.Timestamps);
		PlaybackTrack.LiveLinkRole = Pair.Value.Role;
		PlaybackTrack.SubjectKey = Pair.Key;
		PlaybackTrack.StartIndexOffset = Pair.Value.RecordedDataStartFrame;

		// Don't need to record framerate for static data.
	}

	for (const TPair<FLiveLinkSubjectKey, FLiveLinkRecordingBaseDataContainer>& Pair : RecordingData.FrameData)
	{
		FLiveLinkPlaybackTrack& PlaybackTrack = InOutLiveLinkPlaybackTracks.Tracks.FindOrAdd(Pair.Key);

		const UE::LiveLinkHub::FrameData::Private::FFrameMetaData& MetaData = FrameFileData.FindChecked(Pair.Key);

		PlaybackTrack.FrameData = Pair.Value.RecordedData;
		PlaybackTrack.Timestamps = TConstArrayView<double>(Pair.Value.Timestamps);
		PlaybackTrack.SubjectKey = Pair.Key;
		PlaybackTrack.StartIndexOffset = Pair.Value.RecordedDataStartFrame;
		PlaybackTrack.LocalFrameRate = MetaData.LocalFrameRate;
	}
}

void ULiveLinkUAssetRecording::InitializeNewRecordingData(FLiveLinkUAssetRecordingData&& InRecordingData, double InRecordingLengthSeconds)
{
	RecordingData = MoveTemp(InRecordingData);
	LengthInSeconds = InRecordingLengthSeconds;
	FrameRate = FApp::GetTimecodeFrameRate();
	RecordingPreset->BuildFromClient();

	int32 MaxFrames = 0;
	double MaxLastTimestamp = 0.0;
	for (const TTuple<FLiveLinkSubjectKey, FLiveLinkRecordingBaseDataContainer>& FrameData : RecordingData.FrameData)
	{
		const int32 LocalMaxFrames = FrameData.Value.Timestamps.Num();
		if (LocalMaxFrames > MaxFrames)
		{
			MaxFrames = LocalMaxFrames;
		}

		double LocalLastTimestamp = 0.0;
		if (FrameData.Value.Timestamps.Num() > 0)
		{
			LocalLastTimestamp = FrameData.Value.Timestamps.Last();
			if (LocalLastTimestamp > MaxLastTimestamp)
			{
				MaxLastTimestamp = LocalLastTimestamp;
			}
		}

		// The metadata for a recording already loaded is relevant just for the frame rate.
		UE::LiveLinkHub::FrameData::Private::FFrameMetaData MetaData;
		{
			MetaData.LastTimestamp = LocalLastTimestamp;
			MetaData.MaxFrames = LocalMaxFrames;
			MetaData.LocalFrameRate = CalculateFrameRate(LocalMaxFrames, LocalLastTimestamp);
		}
		FrameFileData.Add(FrameData.Key, MoveTemp(MetaData));
	}

	RecordingMaxFrames = MaxFrames;
	RecordingLastTimestamp = MaxLastTimestamp;
	
	bIsFullyLoaded = true;
}

FFrameRate ULiveLinkUAssetRecording::CalculateFrameRate(const int32 InMaxFrames, const double InTime)
{
	const double FramesPerSecond = InTime > 0.0 ? InMaxFrames / InTime : 0.0;
	const int64 FramesPerSecondInt = FMath::RoundToInt(FramesPerSecond);

	return FFrameRate(static_cast<uint32>(FramesPerSecondInt), 1);
}

void ULiveLinkUAssetRecording::SaveFrameData(FArchive* InFileWriter, const FLiveLinkSubjectKey& InSubjectKey, FLiveLinkRecordingBaseDataContainer& InBaseDataContainer)
{
	// This will crash if it fails -- we don't want to save invalid data.
	InBaseDataContainer.ValidateData();
		
	// Start block with map key.
	FGuid Source = InSubjectKey.Source;
	FString SubjectName = InSubjectKey.SubjectName.ToString();
	int32 NumFrames = InBaseDataContainer.RecordedData.Num();

	// We record the frame header size first, so later we can bulk load the entire block into memory, then feed it to a memory reader.
	const uint64 FrameHeaderSizePosition = InFileWriter->Tell();
	int32 FrameHeaderSize = 0;
	*InFileWriter << FrameHeaderSize;
	const int32 FrameHeaderSizeStart = InFileWriter->Tell();
	
	*InFileWriter << Source;
	*InFileWriter << SubjectName;
	*InFileWriter << NumFrames;

	if (NumFrames == 0)
	{
		UE_LOG(LogLiveLinkHub, Error, TEXT("No frames recorded."));
		return;
	}
	
	const UScriptStruct* ScriptStruct = InBaseDataContainer.RecordedData[0]->GetScriptStruct();
	FString StructTypeName = ScriptStruct->GetPathName();

	// Write the struct name and size so it can be loaded later.
	*InFileWriter << StructTypeName;

	// Write the frame header size.
	{
		const uint64 CurrentPosition = InFileWriter->Tell();
		FrameHeaderSize = CurrentPosition - FrameHeaderSizeStart;

		InFileWriter->Seek(FrameHeaderSizePosition);
		*InFileWriter << FrameHeaderSize;
		InFileWriter->Seek(CurrentPosition);
	}

	// Remember the position to write the frame size.
	const uint64 SerializedFrameSizePosition = InFileWriter->Tell();
	int32 SerializedFrameSize = 0;
	*InFileWriter << SerializedFrameSize;
			
	for (int32 FrameIdx = 0; FrameIdx < NumFrames; ++FrameIdx)
	{
		TSharedPtr<FInstancedStruct>& Frame = InBaseDataContainer.RecordedData[FrameIdx];
		check(Frame.IsValid() && Frame->IsValid());
			
		// Write the frame index for streaming frames when loading.
		*InFileWriter << FrameIdx;

		// Write the frame's timestamp.
		double Timestamp = InBaseDataContainer.Timestamps[FrameIdx];
		*InFileWriter << Timestamp;
			
		// Write the entire frame data.
		uint64 SerializeDataStart = InFileWriter->Tell();
		FObjectAndNameAsStringProxyArchive StructAr(*InFileWriter, false);
		Frame->Serialize(StructAr);

		// Store the serialized frame size, so we can write it once later.
		{
			int32 CurrentSerializedFrameSize = InFileWriter->Tell() - SerializeDataStart;
			// Sanity check that the serialized frame size is consistent.
			ensure(CurrentSerializedFrameSize == SerializedFrameSize || SerializedFrameSize == 0);
			SerializedFrameSize = CurrentSerializedFrameSize;
		}
	}

	if (SerializedFrameSize > 0)
	{
		// Write the frame data offset at the beginning of the block.
		const uint64 FinalOffset = InFileWriter->Tell();
		InFileWriter->Seek(SerializedFrameSizePosition);
		*InFileWriter << SerializedFrameSize;
		InFileWriter->Seek(FinalOffset);
	}
}

static TAutoConsoleVariable<float> CVarLiveLinkHubDebugFrameBufferDelay(
	TEXT("LiveLinkHub.Debug.FrameBufferDelay"),
	0.0f,
	TEXT("The number of seconds to wait when buffering each frame.")
);

void ULiveLinkUAssetRecording::LoadRecordingAsync(int32 InStartFrame, int32 InCurrentFrame, int32 InNumFramesToLoad)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadRecordingAsync"), STAT_ULiveLinkUAssetRecording_LoadRecordingAsync, STATGROUP_LiveLinkHub);

	using namespace UE::LiveLinkHub;
	
	StreamingFrameChangeFromFrame = INDEX_NONE;
	
	const int32 MaxPossibleFrame = RecordingMaxFrames - 1;
	InStartFrame = FMath::Clamp(InStartFrame, 0, MaxPossibleFrame);
	InCurrentFrame = FMath::Clamp(InCurrentFrame, 0, MaxPossibleFrame);
	const int32 EndFrame = InStartFrame + InNumFramesToLoad - 1;

	UE_LOG(LogLiveLinkHub, Verbose, TEXT("Loading recording StartFrame: %d, CurrentFrame: %d, EndFrame: %d "), InStartFrame, InCurrentFrame, EndFrame);
	
	ON_SCOPE_EXIT
	{
		// Always set to true. Some blocking operations wait for this, and in the case of a non-fatal error
		// we want to display error logs and don't want the program to freeze.
		bPerformedInitialLoad = true;
	};
	
	if (IsFrameRangeBuffered(RangeHelpers::Private::MakeInclusiveRange(InStartFrame, FMath::Min(EndFrame, MaxPossibleFrame > 0 ? MaxPossibleFrame : EndFrame))))
	{
		// All frames are already buffered.
		UE_LOG(LogLiveLinkHub, Verbose, TEXT("Frame range already buffered"));
		return;
	}
	
	// Perform initial load and record entry frame file offsets.
	const bool bInitialLoad = FrameFileData.Num() == 0;
	if (bInitialLoad)
	{
		AnimationData.ResetBulkDataOffset();
		
		int32 LoadedRecordingVersion;
		AnimationData.ReadBulkDataPrimitive(LoadedRecordingVersion);

		// If we modify the RecordingVersion we can perform import logic here.
		ensure(LoadedRecordingVersion == RecordingVersion);
			
		// Process static data.
		
		int32 NumStaticData = 0;
		
		AnimationData.ReadBulkDataPrimitive(NumStaticData);

		for (int32 StaticIdx = 0; StaticIdx < NumStaticData; ++StaticIdx)
		{
			// Create framedata just to load initial static frame data. Static data doesn't require this afterward.
			FrameData::Private::FFrameMetaData TemporaryFrameData;
			if (!LoadInitialFrameData(TemporaryFrameData))
			{
				return;
			}
				
			FLiveLinkRecordingStaticDataContainer& DataContainer = RecordingData.StaticData.FindChecked(*TemporaryFrameData.FrameDataSubjectKey.Get());
			LoadFrameData(TemporaryFrameData, DataContainer, 0, 0, 1);

			FScopeLock Lock(&DataContainerMutex);
			MoveFrameDataToContainer(DataContainer, TemporaryFrameData);
		}

		// Process frame data.
		
		int32 NumFrameData = 0;
		AnimationData.ReadBulkDataPrimitive(NumFrameData);

		for (int32 FrameIdx = 0; FrameIdx < NumFrameData; ++FrameIdx)
		{
			FrameData::Private::FFrameMetaData KeyPosition;
			if (!LoadInitialFrameData(KeyPosition))
			{
				return;
			}

			// Offset to the end of this block if there is multiple NumFrameData.
			AnimationData.SetBulkDataOffset(KeyPosition.GetFrameFilePosition(KeyPosition.MaxFrames));
			FrameFileData.Add(*KeyPosition.FrameDataSubjectKey, MoveTemp(KeyPosition));
		}
	}
	
	// Load the required frames, either on initial load or subsequent loads.
	int32 CompletedTasks = 0;
	while (true)
	{
		WaitIfPaused_AsyncThread();
		
		// Break each frame data segment into its own "task" and context switch between them, by buffering up to LiveLinkHubSettings->BufferBatchSize.
		// The goal is to buffer the same frame numbers on multiple sources before reporting the frames as loaded, without delaying overall load
		// too much. This way when a frame number is requested, that frame will be ready across all sources.
		bool bHasCanceled = false;
		for (TTuple<FLiveLinkSubjectKey, FrameData::Private::FFrameMetaData>& FrameDataKeyVal : FrameFileData)
		{
			FrameData::Private::FFrameMetaData& FrameData = FrameDataKeyVal.Value;
			if (FrameData.BufferIterationData.Status <= FrameData::Private::FFrameBufferIterationData::Active)
			{
				if (ensure(FrameData.FrameDataSubjectKey.IsValid()))
				{
					FLiveLinkRecordingBaseDataContainer& DataContainer = RecordingData.FrameData.FindChecked(*FrameData.FrameDataSubjectKey.Get());
					LoadFrameData(FrameData, DataContainer, InStartFrame, InCurrentFrame, InNumFramesToLoad);
					if (FrameData.BufferIterationData.Status == FrameData::Private::FFrameBufferIterationData::Complete)
					{
						CompletedTasks++;
					}
				}
				else
				{
					UE_LOG(LogLiveLinkHub, Error, TEXT("FrameDataSubjectKey is missing for recording %s."), *GetName());
					CompletedTasks++;
				}
			}
			if (FrameData.BufferIterationData.Status == FrameData::Private::FFrameBufferIterationData::Canceled)
			{
				bHasCanceled = true;
				// Don't break, we still want to complete current iterations up to the batch size.
			}
		}

		const bool bCompletedAllTasks = CompletedTasks >= FrameFileData.Num() || bHasCanceled;
		
		// Make data available on the game thread.
		{
			FScopeLock Lock(&DataContainerMutex);
			for (TTuple<FLiveLinkSubjectKey, FrameData::Private::FFrameMetaData>& FrameDataKeyVal : FrameFileData)
			{
				FrameData::Private::FFrameMetaData& FrameData = FrameDataKeyVal.Value;
				MoveFrameDataToContainer(RecordingData.FrameData.FindChecked(*FrameData.FrameDataSubjectKey.Get()), FrameData);
				
				if (bCompletedAllTasks)
				{
					DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::TaskCleanup"), STAT_ULiveLinkUAssetRecording_TaskCleanup, STATGROUP_LiveLinkHub);
					FrameData.BufferedCache.CleanCache(RangeHelpers::Private::MakeInclusiveRange(
					InStartFrame,
					EndFrame
					));

					FrameData.BufferIterationData.Reset();
				}
			}

			UpdateBufferedFrames();
		}
		
		if (bCompletedAllTasks)
		{
			break;
		}
	}
}

bool ULiveLinkUAssetRecording::LoadInitialFrameData(UE::LiveLinkHub::FrameData::Private::FFrameMetaData& OutFrameData)
{
	int32 FrameHeaderSize = 0;
	AnimationData.ReadBulkDataPrimitive(FrameHeaderSize);

	{
		const TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader> Reader = AnimationData.CreateBulkDataMemoryReader(FrameHeaderSize);
	
		FGuid KeySource = FGuid();
		FString KeyName;

		Reader->GetMemoryReader() << KeySource;
		Reader->GetMemoryReader() << KeyName;
			
		OutFrameData.FrameDataSubjectKey = MakeShared<FLiveLinkSubjectKey>(KeySource, *KeyName);
	
		int32 MaxFrames = 0;
		Reader->GetMemoryReader() << MaxFrames;

		if (MaxFrames > RecordingMaxFrames)
		{
			RecordingMaxFrames = MaxFrames;
		}
	
		OutFrameData.MaxFrames = MaxFrames;
			
		if (MaxFrames > 0)
		{	
			FString StructTypeName;
			int32 SerializedStructureSize;

			Reader->GetMemoryReader() << StructTypeName;

			AnimationData.ReadBulkDataPrimitive(SerializedStructureSize);

			OutFrameData.SerializedStructureSize = SerializedStructureSize;
			OutFrameData.RecordingStartFrameFilePosition = AnimationData.GetBulkDataOffset();

			OutFrameData.LoadedStruct = FindObject<UScriptStruct>(nullptr, *StructTypeName, true);
			if (!OutFrameData.LoadedStruct.IsValid())
			{
				UE_LOG(LogLiveLinkHub, Error, TEXT("Script struct type '%s' not found."), *StructTypeName);
				return false;
			}

			// The size on disk for each frame-- consisting of the frame index, timestamp, and frame struct data.
			OutFrameData.FrameDiskSize = (sizeof(int32) + sizeof(double) + SerializedStructureSize);

			if (OutFrameData.FrameDiskSize > MaxFrameDiskSize)
			{
				MaxFrameDiskSize = OutFrameData.FrameDiskSize;
			}
		}
	}

	// Find the last timestamp, this is so we can calculate the correct framerate for this track.
	{
		const int32 LastFrame = OutFrameData.MaxFrames - 1;
		double Timestamp;
		LoadTimestampFromDisk(LastFrame, OutFrameData, Timestamp);
		OutFrameData.LastTimestamp = Timestamp;

		if (Timestamp > RecordingLastTimestamp)
		{
			RecordingLastTimestamp = Timestamp;
		}
	}

	// Calculate frame rate.
	OutFrameData.LocalFrameRate = CalculateFrameRate(OutFrameData.MaxFrames, OutFrameData.LastTimestamp);
	
	return true;
}

void ULiveLinkUAssetRecording::LoadFrameData(UE::LiveLinkHub::FrameData::Private::FFrameMetaData& InFrameData, FLiveLinkRecordingBaseDataContainer& InDataContainer,
                                             int32 RequestedStartFrame, int32 RequestedInitialFrame, int32 RequestedFramesToLoad)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadFrameData"), STAT_ULiveLinkUAssetRecording_LoadFrameData, STATGROUP_LiveLinkHub);

	using namespace UE::LiveLinkHub;
	
	FrameData::Private::FFrameBufferIterationData& IterationData = InFrameData.BufferIterationData;

	// First, localize the frame times to this frame data.
	if (InFrameData.LocalFrameRate.IsValid())
	{
		const FFrameRate RecordingFrameRate = GetGlobalFrameRate();
		const FQualifiedFrameTime StartFrameTime(FFrameTime(RequestedStartFrame), RecordingFrameRate);
		const FQualifiedFrameTime StartInitialFrameTime(FFrameTime(RequestedInitialFrame), RecordingFrameRate);

		RequestedStartFrame = StartFrameTime.ConvertTo(InFrameData.LocalFrameRate).FrameNumber.Value;
		RequestedInitialFrame = StartInitialFrameTime.ConvertTo(InFrameData.LocalFrameRate).FrameNumber.Value;
	}
	
	int32 MaxFrames = InFrameData.MaxFrames;
	if (MaxFrames > 0)
	{
		if (RequestedFramesToLoad > 0)
		{
			// Don't go past requested frames or max frames.
			MaxFrames = FMath::Min(MaxFrames, RequestedStartFrame + RequestedFramesToLoad);
		}

		const int32 LastFrame = MaxFrames - 1;
		check(LastFrame >= 0);
		
		IterationData.ForwardData.Reset();
		IterationData.ReverseData.Reset();

		const bool bIsNewIteration = IterationData.Status == FrameData::Private::FFrameBufferIterationData::New;
		
		// Load each frame from the initial frame, alternating right to left each frame. This creates a buffer to support
		// scrubbing each direction and makes sure the immediate frames are loaded first.

		int32 LastLoadedRightFrame, LastLoadedLeftFrame;
		
		if (bIsNewIteration)
		{
			LastLoadedRightFrame = RequestedInitialFrame;
			LastLoadedLeftFrame = RequestedInitialFrame;
			IterationData.Status = FrameData::Private::FFrameBufferIterationData::Active;
		}
		else
		{
			// We don't need to convert, as these are already localized.
			LastLoadedLeftFrame = IterationData.LastLoadedLeftFrame;
			LastLoadedRightFrame = IterationData.LastLoadedRightFrame + 1;
		}
		
		int32 RightFrameIdx = LastLoadedRightFrame;
		int32 LeftFrameIdx = LastLoadedLeftFrame - 1; // - 1 So we don't try to load the same initial frame when alternating to the left.

		bool bLoadRight = IterationData.bLoadRight; // Start right -> left
		
		// We could potentially optimize this further -- such as adjusting the ratio of ahead/behind frames to buffer based on whether
		// the recording is playing forward or reverse vs being scrubbed.

		// Clear loaded frames that aren't part of the required range.
		if (bIsNewIteration)
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadFrameData::NewIteration"), STAT_ULiveLinkUAssetRecording_LoadFrameData_NewIteration, STATGROUP_LiveLinkHub);
			
			FScopeLock DataContainerLock(&DataContainerMutex);

			TRange<int32> RangeToLoad = RangeHelpers::Private::MakeInclusiveRange(RequestedStartFrame, LastFrame);
			TRange<int32> LoadedRange = InDataContainer.GetBufferedFrames();
			
			RangeHelpers::Private::TRangeArray<int32> FramesToUnload;
			{
				if (LoadedRange.GetLowerBoundValue() < RangeToLoad.GetLowerBoundValue())
				{
					TRange<int32> BeforeRange = RangeHelpers::Private::MakeInclusiveRange(LoadedRange.GetLowerBoundValue(), FMath::Min(RangeToLoad.GetLowerBoundValue(),LoadedRange.GetUpperBoundValue()));
					FramesToUnload.Add(BeforeRange);
				}

				if (LoadedRange.GetUpperBoundValue() > RangeToLoad.GetUpperBoundValue())
				{
					TRange<int32> AfterRange = RangeHelpers::Private::MakeInclusiveRange(FMath::Max(RangeToLoad.GetUpperBoundValue(), LoadedRange.GetLowerBoundValue()), LoadedRange.GetUpperBoundValue());
					FramesToUnload.Add(AfterRange);
				}
			}
			
			for (const TRange<int32>& Range : FramesToUnload)
			{
				MoveRangeToCache(Range, InDataContainer, InFrameData);
			}

			LoadedRange = InDataContainer.GetBufferedFrames();
			if (!LoadedRange.Contains(InitialFrameToStream))
			{
				// In this case we aren't immediately loading frames, but will get to them eventually.
				TRange<int32> Intersection = TRange<int32>::Intersection(RangeToLoad, LoadedRange);
				MoveRangeToCache(Intersection, InDataContainer, InFrameData);
				
				LoadedRange = InDataContainer.GetBufferedFrames();
			}

			// If the range is partially loaded, default left/right load to loaded values to save iteration time.
			if (!LoadedRange.IsEmpty())
			{
				LastLoadedLeftFrame = LoadedRange.GetLowerBoundValue();
				LastLoadedRightFrame = LoadedRange.GetUpperBoundValue();
				LeftFrameIdx = LastLoadedLeftFrame;
				RightFrameIdx = LastLoadedRightFrame;
			}
		}
		
		// Verify we don't exceed the last frame, such as if this particular frame data ends before the recording's desired frame.
		{
			RightFrameIdx = FMath::Min(RightFrameIdx, LastFrame);
			LastLoadedRightFrame = FMath::Min(LastLoadedRightFrame, LastFrame);

			LeftFrameIdx = FMath::Min(LeftFrameIdx, LastFrame);
			LastLoadedLeftFrame = FMath::Min(LastLoadedLeftFrame, LastFrame);
		}

		if (LeftFrameIdx == RightFrameIdx)
		{
			// Don't try loading the same frame.
			RightFrameIdx++;
		}
		
		auto AlternateLoadDirection = [&]()
		{
			bLoadRight = !bLoadRight;
		};

		const int32 BufferBatchSize = GetDefault<ULiveLinkHubSettings>()->BufferBatchSize;

		struct FBulkDataBatch
		{
			/** Reader containing unprocessed bulk data segment in memory. */
			TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader> Reader;
			/** Minimum frame of the batch. */
			int32 MinFrame = 0;
			/** Maximum frame of the batch. */
			int32 MaxFrame = 0;
			/** Total frame count of the batch. Mainly to help if a batch is empty or not. */
			int32 Count = 0;
			/** If a frame number is included in this batch. */
			bool ContainsFrame(const int32 InFrame) const
			{
				return InFrame >= MinFrame && InFrame <= MaxFrame && Count != 0;
			}
		};

		// Determine ranges of disk data for left and right batches. The data must be contiguous, and will only
		// extend until a preloaded frame is found. These batches are so we can read multiple frames from bulk data at once,
		// saving on disk load time compared to loading the frames individually. Any data loaded here must still be processed
		// per-frame through a serializer.
		FBulkDataBatch RawFramesLeftBatch, RawFramesRightBatch;
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadFrameData::CreateBatches"), STAT_ULiveLinkUAssetRecording_LoadFrameData_CreateBatches, STATGROUP_LiveLinkHub);

			// Initial allocation, splitting evenly between left and right.
			int32 HalfBatchSize = BufferBatchSize / 2;
			int32 LeftBatchSize = HalfBatchSize;
			int32 RightBatchSize = BufferBatchSize - HalfBatchSize; // Remainder goes to the right.

			const int32 AvailableLeftFrames = FMath::Max(LeftFrameIdx - RequestedStartFrame, 0);
			const int32 AvailableRightFrames = FMath::Max(MaxFrames - RightFrameIdx, 0);
		
			if (AvailableLeftFrames < LeftBatchSize)
			{
				RightBatchSize += (LeftBatchSize - AvailableLeftFrames); // Shift extra to the right.
				LeftBatchSize = AvailableLeftFrames;
			}

			if (AvailableRightFrames < RightBatchSize)
			{
				LeftBatchSize += (RightBatchSize - AvailableRightFrames); // Shift extra to the left.
				RightBatchSize = AvailableRightFrames;
			}

			// Make sure we're still within limits.
			LeftBatchSize = FMath::Clamp(LeftBatchSize, 0, AvailableLeftFrames);
			RightBatchSize = FMath::Clamp(RightBatchSize, 0, AvailableRightFrames);
			
			// Determine the min/max frames for each batch.
			if (LeftBatchSize > 0)
			{
				RawFramesLeftBatch.MinFrame = LeftFrameIdx - LeftBatchSize + 1;
				RawFramesLeftBatch.MaxFrame = LeftFrameIdx;

				// Shrink the batch to the first loaded frame, left to right since it's more likely frames for the left batch
				// will already be loaded the more to the right (center).
				for (int32 Idx = RawFramesLeftBatch.MinFrame; Idx < RawFramesLeftBatch.MaxFrame; ++Idx)
				{
					if (InDataContainer.IsFrameLoaded(Idx) || InFrameData.BufferedCache.ContainsFrame(Idx))
					{
						const int32 Remaining = (RawFramesLeftBatch.MaxFrame - Idx);
						LeftBatchSize -= Remaining;
						RawFramesLeftBatch.MaxFrame = Idx - 1;
						break;
					}
				}
				if (LeftBatchSize > 0)
				{
					RawFramesLeftBatch.Reader = LoadRawFramesFromDisk(RawFramesLeftBatch.MinFrame, LeftBatchSize, InFrameData);
				}
			}
			RawFramesLeftBatch.Count = FMath::Max(LeftBatchSize, 0);

			if (RightBatchSize > 0)
			{
				RawFramesRightBatch.MinFrame = RightFrameIdx;
				RawFramesRightBatch.MaxFrame = RightFrameIdx + RightBatchSize - 1;

				// Shrink the batch to the first loaded frame, right to left since it's more likely frames for the right batch
				// will already be loaded the more to the left (center).
				for (int32 Idx = RawFramesRightBatch.MaxFrame; Idx >= RawFramesRightBatch.MinFrame; --Idx)
				{
					if (InDataContainer.IsFrameLoaded(Idx) || InFrameData.BufferedCache.ContainsFrame(Idx))
					{
						const int32 Remaining = (Idx - RawFramesRightBatch.MinFrame) + 1;
						RightBatchSize -= Remaining;
						RawFramesRightBatch.MinFrame = Idx + 1;
						
						break;
					}
				}
				if (RightBatchSize > 0)
				{
					RawFramesRightBatch.Reader = LoadRawFramesFromDisk(RawFramesRightBatch.MinFrame, RightBatchSize, InFrameData);
				}
			}
			RawFramesRightBatch.Count = FMath::Max(RightBatchSize, 0);
		}

		// Iterate through the entire range, loading frames from cache or disk. This runs until a batch cycle has completed
		// or the entire range is loaded.
		int32 FramesLoaded = 0;
		while (RightFrameIdx < MaxFrames || LeftFrameIdx >= RequestedStartFrame)
		{
			if (bCancelStream)
			{
				break;
			}
			
			int32 FrameToLoad;
    
			if (bLoadRight)
			{
				if (RightFrameIdx >= MaxFrames)
				{
					bLoadRight = false;
					continue;
				}
				FrameToLoad = RightFrameIdx++;
				LastLoadedRightFrame = FrameToLoad;
			}
			else
			{
				if (LeftFrameIdx < RequestedStartFrame)
				{
					bLoadRight = true;
					continue;
				}
				FrameToLoad = LeftFrameIdx--;
				LastLoadedLeftFrame = FrameToLoad;
			}
			
			auto InsertFrame = [&](const TSharedPtr<FInstancedStruct>& InFrame, double InTimestamp)
			{
#if LIVELINKHUB_FRAME_DEBUG
				ensureAlways(!InFrameData.BufferIterationData.ForwardData.Timestamps.Contains(InTimestamp));
				ensureAlways(!InFrameData.BufferIterationData.ReverseData.Timestamps.Contains(InTimestamp));

				ensureAlways(!InDataContainer.Timestamps.Contains(InTimestamp));
				
				// Additional validation to ensure timestamps / frames are loaded in the correct order.
				for (int32 Idx = 1; Idx < InFrameData.BufferIterationData.ForwardData.Timestamps.Num(); ++Idx)
				{
					double LastTimestamp = InFrameData.BufferIterationData.ForwardData.Timestamps[Idx - 1];
					double CurrentTimestamp = InFrameData.BufferIterationData.ForwardData.Timestamps[Idx];
					ensureAlways(LastTimestamp < CurrentTimestamp);
				}
				for (int32 Idx = 1; Idx < InFrameData.BufferIterationData.ReverseData.Timestamps.Num(); ++Idx)
				{
					double LastTimestamp = InFrameData.BufferIterationData.ReverseData.Timestamps[Idx - 1];
					double CurrentTimestamp = InFrameData.BufferIterationData.ReverseData.Timestamps[Idx];
					ensureAlways(LastTimestamp < CurrentTimestamp);
				}
#endif
				if (bLoadRight)
				{
					InFrameData.BufferIterationData.ForwardData.Timestamps.Add(InTimestamp);
					InFrameData.BufferIterationData.ForwardData.RecordedData.Add(InFrame);
				}
				else
				{
					InFrameData.BufferIterationData.ReverseData.Timestamps.Insert(InTimestamp, 0);
					InFrameData.BufferIterationData.ReverseData.RecordedData.Insert(InFrame, 0);
				}
			};

			bool bIsFrameFullyLoaded = false;
			{
				DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadFrameData::FindCachedFrame"), STAT_ULiveLinkUAssetRecording_LoadFrameData_FindCachedFrame, STATGROUP_LiveLinkHub);

				double ExistingTimestamp = 0.f;
				if (InDataContainer.IsFrameLoaded(FrameToLoad))
				{
					// Frame is in-memory and part of the active range.
					bIsFrameFullyLoaded = true;
				}
				else if (TSharedPtr<FInstancedStruct> ExistingFrame = InFrameData.BufferedCache.TryGetCachedFrame(FrameToLoad, ExistingTimestamp))
				{
					// Frame is still in memory, but was pending deletion. Move to the active range.
					InsertFrame(ExistingFrame, ExistingTimestamp);
					bIsFrameFullyLoaded = true;
				}
			}
			
			if (!bIsFrameFullyLoaded)
			{
				// Frame needs to be deserialized, and potentially loaded from disk.
				
				double Timestamp;
				TSharedPtr<FInstancedStruct> DataInstancedStruct;
				TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader> MemoryToUse;
				// Check if we have preloaded the raw data into memory.
				{
					int64 FramePosition = 0;
					if (RawFramesRightBatch.ContainsFrame(FrameToLoad))
					{
						MemoryToUse = RawFramesRightBatch.Reader;
						check(!RawFramesLeftBatch.ContainsFrame(FrameToLoad));
						FramePosition = InFrameData.GetRelativeFrameFilePosition(FrameToLoad - RawFramesRightBatch.MinFrame);
					}
					else if (RawFramesLeftBatch.ContainsFrame(FrameToLoad))
					{
						MemoryToUse = RawFramesLeftBatch.Reader;
						FramePosition = InFrameData.GetRelativeFrameFilePosition(FrameToLoad - RawFramesLeftBatch.MinFrame);
					}

					if (MemoryToUse.IsValid())
					{
						// The position should ideally be correct, but it's possible with various caching it is off.
						MemoryToUse->GetMemoryReader().Seek(FramePosition);
					}
				}
			
				if (!ensure(LoadFrameFromDisk(FrameToLoad, InFrameData, DataInstancedStruct, Timestamp, MemoryToUse)))
				{
					continue;
				}

				InsertFrame(DataInstancedStruct, Timestamp);
			}
			ensure(InDataContainer.Timestamps.Num() == InDataContainer.RecordedData.Num());
			
			FramesLoaded++;
			AlternateLoadDirection();
			
			// Break the loop when enough frames have finished constituting a batch of frames.
			if (FramesLoaded > 0 && FramesLoaded % BufferBatchSize == 0)
			{
				break;
			}
		}

		IterationData.bLoadRight = bLoadRight;
		IterationData.LastLoadedLeftFrame = LastLoadedLeftFrame;
		IterationData.LastLoadedRightFrame = LastLoadedRightFrame;

		// Determine the iteration status, which will impact the overall async load loop.
		if (RightFrameIdx >= MaxFrames && LeftFrameIdx <= RequestedStartFrame)
		{
			IterationData.Status = FrameData::Private::FFrameBufferIterationData::Complete;
		}
		else if (bCancelStream || StreamingFrameChangedRequested())
		{
			IterationData.Status = FrameData::Private::FFrameBufferIterationData::Canceled;
		}
	}
}

bool ULiveLinkUAssetRecording::LoadFrameFromDisk(const int32 InFrame, const UE::LiveLinkHub::FrameData::Private::FFrameMetaData& InFrameData,
	TSharedPtr<FInstancedStruct>& OutFrame, double& OutTimestamp, const TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader>& InMemory)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadFrameFromDisk"), STAT_ULiveLinkUAssetRecording_LoadFrameFromDisk, STATGROUP_LiveLinkHub);
	OutTimestamp = 0.0;

	TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader> Reader;
	
	// Either use preloaded memory if passed in, or load from bulk data directly.
	if (InMemory.IsValid())
	{
		Reader = InMemory;
	}
	else
	{
		const int64 FramePosition = InFrameData.GetFrameFilePosition(InFrame);
		AnimationData.SetBulkDataOffset(FramePosition);
		Reader = AnimationData.CreateBulkDataMemoryReader(InFrameData.FrameDiskSize);
	}

	int32 ParsedFrameIdx = 0;
	Reader->GetMemoryReader() << ParsedFrameIdx;

	// Ensure the parsed frame index matches the expected frame
	if (!ensure(ParsedFrameIdx == InFrame))
	{
		UE_LOG(LogLiveLinkHub, Error, TEXT("Frame index mismatch: expected %d, got %d"), InFrame, ParsedFrameIdx);
		return false;
	}
			
	double Timestamp = 0;
	Reader->GetMemoryReader() << Timestamp;

	// Instantiate the animation frame.
	UScriptStruct* LoadedStruct = InFrameData.LoadedStruct.Get();
	if (LoadedStruct == nullptr)
	{
		UE_LOG(LogLiveLinkHub, Error, TEXT("Script struct type not found."));
		return false;
	}

	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadFrameFromDisk::SerializeFrame"),
			STAT_ULiveLinkUAssetRecording_LoadFrameFromDisk_SerializeFrame, STATGROUP_LiveLinkHub);
		FObjectAndNameAsStringProxyArchive StructAr(Reader->GetMemoryReader(), true);
		OutFrame = MakeShared<FInstancedStruct>(LoadedStruct);
		OutFrame->Serialize(StructAr);
	}
	
	OutTimestamp = Timestamp;

	return true;
}

bool ULiveLinkUAssetRecording::LoadTimestampFromDisk(const int32 InFrame, const UE::LiveLinkHub::FrameData::Private::FFrameMetaData& InFrameData, double& OutTimestamp)
{
	const int64 FramePosition = InFrameData.GetFrameFilePosition(InFrame);
	AnimationData.SetBulkDataOffset(FramePosition);
			
	const TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader> Reader = AnimationData.CreateBulkDataMemoryReader(InFrameData.FrameDiskSize);

	int32 ParsedFrameIdx = 0;
	Reader->GetMemoryReader() << ParsedFrameIdx;

	// Ensure the parsed frame index matches the expected frame
	if (!ensure(ParsedFrameIdx == InFrame))
	{
		UE_LOG(LogLiveLinkHub, Error, TEXT("Frame index mismatch: expected %d, got %d"), InFrame, ParsedFrameIdx);
		return false;
	}
			
	Reader->GetMemoryReader() << OutTimestamp;

	return true;
}

TSharedPtr<FLiveLinkHubBulkData::FScopedBulkDataMemoryReader> ULiveLinkUAssetRecording::LoadRawFramesFromDisk(const int32 InFrame,
	const int32 InNumFrames, const UE::LiveLinkHub::FrameData::Private::FFrameMetaData& InFrameData)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::LoadRawFramesFromDisk"), STAT_ULiveLinkUAssetRecording_LoadRawFramesFromDisk, STATGROUP_LiveLinkHub);

	// Seek to the beginning of the frames to load.
	const int64 FramePosition = InFrameData.GetFrameFilePosition(InFrame);
	AnimationData.SetBulkDataOffset(FramePosition);

	// Make sure the max frames don't exceed the maximum frames for this source.
	const int32 MaxFrames = FMath::Min(InNumFrames, InFrameData.MaxFrames - InFrame);
	check(MaxFrames >= 1);

	// Determine complete byte size to load.
	const int32 MaxByteSize = MaxFrames * InFrameData.FrameDiskSize;
	return AnimationData.CreateBulkDataMemoryReader(MaxByteSize);
}

void ULiveLinkUAssetRecording::EjectAndUnload()
{
	const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
	if (const TSharedPtr<FLiveLinkHubPlaybackController> Controller = LiveLinkHubModule.GetPlaybackController())
	{
		Controller->EjectAndUnload(nullptr, this);
	}
}

void ULiveLinkUAssetRecording::MoveFrameDataToContainer(FLiveLinkRecordingBaseDataContainer& InDataContainer,
	UE::LiveLinkHub::FrameData::Private::FFrameMetaData& InFrameData) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::MoveFrameDataToContainer"), STAT_ULiveLinkUAssetRecording_MoveFrameDataToContainer, STATGROUP_LiveLinkHub);

	// Don't mutex lock here, it should be locked from the calling method so multiple moves can be done under one lock.
	
#if LIVELINKHUB_FRAME_DEBUG
	// Additional validation to ensure timestamps / frames are loaded in the correct order.
	for (int32 Idx = 0; Idx < InFrameData.BufferIterationData.ForwardData.Timestamps.Num(); ++Idx)
	{
		ensureAlways(!InDataContainer.Timestamps.Contains(InFrameData.BufferIterationData.ForwardData.Timestamps[Idx]));
	}
#endif

	InDataContainer.RecordedDataStartFrame = InFrameData.BufferIterationData.LastLoadedLeftFrame;
	
	InDataContainer.Timestamps.Insert(MoveTemp(InFrameData.BufferIterationData.ReverseData.Timestamps), 0);
	InDataContainer.RecordedData.Insert(MoveTemp(InFrameData.BufferIterationData.ReverseData.RecordedData), 0);

	InDataContainer.Timestamps.Append(MoveTemp(InFrameData.BufferIterationData.ForwardData.Timestamps));
	InDataContainer.RecordedData.Append(MoveTemp(InFrameData.BufferIterationData.ForwardData.RecordedData));
			
#if LIVELINKHUB_FRAME_DEBUG
	// Additional validation to ensure timestamps / frames are loaded in the correct order.
	for (int32 Idx = 1; Idx < InDataContainer.Timestamps.Num(); ++Idx)
	{
		double LastTimestamp = InDataContainer.Timestamps[Idx - 1];
		double CurrentTimestamp = InDataContainer.Timestamps[Idx];
		ensureAlways(LastTimestamp < CurrentTimestamp);
	}
#endif
}

void ULiveLinkUAssetRecording::MoveRangeToCache(const TRange<int32>& InRange, FLiveLinkRecordingBaseDataContainer& InDataContainer,
	UE::LiveLinkHub::FrameData::Private::FFrameMetaData& InFrameData) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::MoveRangeToCache"), STAT_ULiveLinkUAssetRecording_MoveRangeToCache, STATGROUP_LiveLinkHub);
	if (!InRange.IsEmpty())
	{
		const int32 StartIndex = InRange.GetLowerBoundValue() - InDataContainer.RecordedDataStartFrame;
		const int32 CountToRemove = UE::LiveLinkHub::RangeHelpers::Private::GetRangeLength(InRange);

		if (StartIndex >= InDataContainer.Timestamps.Num()
			|| (StartIndex + CountToRemove > InDataContainer.Timestamps.Num())
			|| StartIndex < 0
			|| InDataContainer.Timestamps.IsEmpty())
		{
			return;
		}
					
		// Move to cache. This will be unloaded later.
		{
			FLiveLinkRecordingBaseDataContainer HistoryContainer;
			HistoryContainer.Timestamps.Reserve(CountToRemove);
			HistoryContainer.RecordedData.Reserve(CountToRemove);

			HistoryContainer.Timestamps.Append(&InDataContainer.Timestamps[StartIndex], CountToRemove);
			HistoryContainer.RecordedData.Append(&InDataContainer.RecordedData[StartIndex], CountToRemove);
			HistoryContainer.RecordedDataStartFrame = InDataContainer.RecordedDataStartFrame + StartIndex;

			InFrameData.BufferedCache.FrameData.Add(MoveTemp(HistoryContainer));

			InFrameData.BufferedCache.TrimCache();
		}
					
		InDataContainer.Timestamps.RemoveAt(StartIndex, CountToRemove);
		InDataContainer.RecordedData.RemoveAt(StartIndex, CountToRemove);

		if (StartIndex == 0)
		{
			if (InDataContainer.Timestamps.Num() == 0)
			{
				InDataContainer.RecordedDataStartFrame = 0;
			}
			else
			{
				InDataContainer.RecordedDataStartFrame += CountToRemove;
			}
		}
	}
}

void ULiveLinkUAssetRecording::UpdateBufferedFrames()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::UpdateBufferedFrames"), STAT_ULiveLinkUAssetRecording_UpdateBufferedFrames, STATGROUP_LiveLinkHub);

	using namespace UE::LiveLinkHub;
	
	const FFrameRate GlobalFrameRate = GetGlobalFrameRate();
	
	FScopeLock Lock(&BufferedFrameMutex);

	// Find all buffered ranges and convert them to the global frame rate. We don't want to report the local frames that are buffered, but
	// the ranges as viewed by the recording/scrubber.
	BufferedFrameRanges.Reset();
	for (TTuple<FLiveLinkSubjectKey, FrameData::Private::FFrameMetaData>& FrameDataKeyVal : FrameFileData)
	{
		FrameData::Private::FFrameMetaData& FrameData = FrameDataKeyVal.Value;
		RangeHelpers::Private::TRangeArray<int32> CacheBufferRanges = FrameData.BufferedCache.GetCacheBufferRanges();
		for (TRange<int32>& Range : CacheBufferRanges)
		{
			Range = RangeHelpers::Private::ConvertRangeFrameRate(Range, FrameData.LocalFrameRate, GlobalFrameRate);
		}
		
		TRange<int32> ActiveBufferFrames = RecordingData.FrameData.FindChecked(*FrameData.FrameDataSubjectKey.Get()).GetBufferedFrames();
		ActiveBufferFrames = RangeHelpers::Private::ConvertRangeFrameRate(ActiveBufferFrames, FrameData.LocalFrameRate, GlobalFrameRate);
		
		BufferedFrameRanges.Append(MoveTemp(CacheBufferRanges));
		BufferedFrameRanges.Add(MoveTemp(ActiveBufferFrames));
	}
}

void ULiveLinkUAssetRecording::WaitIfPaused_AsyncThread()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ULiveLinkUAssetRecording::WaitIfPaused_AsyncThread"), STAT_ULiveLinkUAssetRecording_WaitIfPaused_AsyncThread, STATGROUP_LiveLinkHub);
	
	check(!IsInGameThread());
	
	if (bPauseStream)
	{
		OnStreamPausedEvent->Trigger();
		OnStreamUnpausedEvent->Wait();
	}
}

void ULiveLinkUAssetRecording::PauseStream()
{
	if (AsyncStreamTask.IsValid() && !AsyncStreamTask->IsDone())
	{
		OnStreamUnpausedEvent->Reset();
		bPauseStream = true;
		OnStreamPausedEvent->Wait();
	}
}

void ULiveLinkUAssetRecording::UnpauseStream()
{
	bPauseStream = false;
	OnStreamPausedEvent->Reset();
	OnStreamUnpausedEvent->Trigger();
}

void ULiveLinkUAssetRecording::OnPreGarbageCollect()
{
	PauseStream();
}

void ULiveLinkUAssetRecording::OnPostGarbageCollect()
{
	UnpauseStream();
}

void ULiveLinkUAssetRecording::FLiveLinkStreamAsyncTask::DoWork()
{
	int32 LastStartFrame = -1;
	int32 LastTotalFrames = -1;
	int32 LastInitialFrame = -1;
	while (LiveLinkRecording && !LiveLinkRecording->bCancelStream)
	{
		if (LastStartFrame != LiveLinkRecording->EarliestFrameToStream
			|| LastTotalFrames != LiveLinkRecording->TotalFramesToStream
			|| LastInitialFrame != LiveLinkRecording->InitialFrameToStream)
		{
			LastStartFrame = LiveLinkRecording->EarliestFrameToStream;
			LastTotalFrames = LiveLinkRecording->TotalFramesToStream;
			LastInitialFrame = LiveLinkRecording->InitialFrameToStream;
			LiveLinkRecording->LoadRecordingAsync(LiveLinkRecording->EarliestFrameToStream,
				LiveLinkRecording->InitialFrameToStream, LiveLinkRecording->TotalFramesToStream);
		}
		else
		{
			LiveLinkRecording->WaitIfPaused_AsyncThread();
			FPlatformProcess::Sleep(0.002);
		}
	}
}

#undef LIVELINKHUB_FRAME_DEBUG