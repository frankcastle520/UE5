// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkRecordingDataContainer.h"
#include "Recording/LiveLinkRecordingRangeHelpers.h"
#include "StructUtils/InstancedStruct.h"

namespace UE::LiveLinkHub::FrameData::Private
{
	/** Data manager for frames which have been loaded. */
	struct FFrameBufferCache
	{
		/** Loaded frame data. */
		TArray<FLiveLinkRecordingBaseDataContainer> FrameData;

		/** Ensure the size of the cache is limited, removing older entries. */
		void TrimCache();

		/** Remove data from the cache that is no longer needed. */
		void CleanCache(const TRange<int32>& InActiveRange);

		/** Return all ranges contained within the cache. */
		RangeHelpers::Private::TRangeArray<int32> GetCacheBufferRanges() const;

		/** Look through the cache for a loaded frame. */
		TSharedPtr<FInstancedStruct> TryGetCachedFrame(const int32 InFrame, double& OutTimestamp);

		/** Checks if a frame is contained within the cache. */
		bool ContainsFrame(const int32 InFrame) const;
	};

	/** Keeps track of current frame iterations while buffering. */
	struct FFrameBufferIterationData
	{
		enum ECompletionStatus
		{
			// New iteration, no processing has performed yet.
			New,
			// Iteration is currently active.
			Active,
			// Iteration has been canceled for any reason.
			Canceled,
			// Iteration has fully completed, buffering all available requested frames.
			Complete
		};

		/** Temporary storage for loaded frames, which gets moved to data containers after loading. */
		struct FTemporaryData
		{
			/** Fully processed frame data. */
			TArray<TSharedPtr<FInstancedStruct>> RecordedData;
			/** Timestamps corresponding to the frame data. */
			TArray<double> Timestamps;
			void Reset()
			{
				RecordedData.Empty();
				Timestamps.Empty();
			}
		};

		/** The last loaded left frame. */
		int32 LastLoadedLeftFrame;
		/** The last loaded right frame. */
		int32 LastLoadedRightFrame;
		/** If we are loading to the right. */
		bool bLoadRight;
		/** Current status of this iteration. */
		ECompletionStatus Status;

		/** Forward-looking frame data. */
		FTemporaryData ForwardData;
		/** Reverse looking frame data. */
		FTemporaryData ReverseData;

		FFrameBufferIterationData()
		{
			Reset();
		}

		/** Resets the iteration to a new iteration, clearing all storage. */
		void Reset()
		{
			Status = New;
			LastLoadedLeftFrame = INDEX_NONE;
			LastLoadedRightFrame = INDEX_NONE;
			bLoadRight = true;
			ForwardData.Reset();
			ReverseData.Reset();
		}
	};
	
	/** Frame data information when loading from a recording file. */
	struct FFrameMetaData
	{
		/** The subject key used for the frame data. */
		TSharedPtr<FLiveLinkSubjectKey> FrameDataSubjectKey;
		/** The struct for this frame data. */
		TWeakObjectPtr<UScriptStruct> LoadedStruct;
		/** The position in the file recording where frame data begins. */
		int64 RecordingStartFrameFilePosition = 0;
		/** Maximum number of frames. */
		int32 MaxFrames = 0;
		/** Total size of the structure. */
		int32 SerializedStructureSize = 0;
		/** The size in bytes of each animation frame. */
		int32 FrameDiskSize;
		/** The last timestamp for this frame data. */
		double LastTimestamp = 0.f;
		/** The frame rate, based only off of number of frames and the last timestamp. */
		FFrameRate LocalFrameRate;
		/** Cache of previously buffered frames, which are not currently active. */
		FFrameBufferCache BufferedCache;
		/** Current iteration data while buffering. */
		FFrameBufferIterationData BufferIterationData;
	
		/** Find the correct file offset based on the frame index. */
		int64 GetFrameFilePosition(const int32 InFrameIdx) const
		{
			return RecordingStartFrameFilePosition + GetRelativeFrameFilePosition(InFrameIdx);
		};

		/** Find the offset relative to local storage only, not accounting for disk position. */
		int64 GetRelativeFrameFilePosition(const int32 InFrameIdx) const
		{
			return FrameDiskSize * InFrameIdx;
		};
	};
}