// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ChaosArchive.h"
#include "Chaos/ParticleHandleFwd.h"
#include "ChaosVDRecording.h"
#include "Containers/Array.h"
#include "Containers/Queue.h"
#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Templates/SharedPointer.h"
#include "TraceServices/Model/AnalysisSession.h"

class FChaosVDDataProcessorBase;

namespace Chaos::VisualDebugger
{
	class FChaosVDSerializableNameTable;
}

struct FChaosVDGameFrameData;
class FChaosVDEngine;
struct FChaosVDSolverFrameData;
struct FChaosVDRecording;
class IChaosVDDataProcessor;

struct FChaosVDBinaryDataContainer
{
	explicit FChaosVDBinaryDataContainer(const int32 InDataID)
		: DataID(InDataID)
	{
	}

	int32 DataID;
	bool bIsReady = false;
	bool bIsCompressed = false;
	uint32 UncompressedSize = 0;
	FString TypeName;
	TArray<uint8> RawData;
};

struct FChaosVDTraceSessionData
{
	TSharedPtr<FChaosVDRecording> InternalRecordingsMap;

	TMap<int32, TSharedPtr<FChaosVDBinaryDataContainer>> UnprocessedDataByID;
};

enum class EChaosVDSolverStageAccessorFlags : uint8
{
	None = 0,
	/* If the solver frame has valid stage data but the last stage is closed, create a new stage which will be labeled as non-staged data */
	CreateNewIfClosed = 1 << 0,
	/* If the solver frame does not have any solver stage data, create a new stage which will be labeled as non-staged data */
	CreateNewIfEmpty = 1 << 1 
};
ENUM_CLASS_FLAGS(EChaosVDSolverStageAccessorFlags);

/** Provider class for Chaos VD trace recordings.
 * It stores and handles rebuilt recorded frame data from Trace events
 * dispatched by the Chaos VD Trace analyzer
 */
class FChaosVDTraceProvider : public TraceServices::IProvider, public TSharedFromThis<FChaosVDTraceProvider>
{
public:
	
	static FName ProviderName;

	FChaosVDTraceProvider(TraceServices::IAnalysisSession& InSession);

	void CreateRecordingInstanceForSession(const FString& InSessionName);
	void DeleteRecordingInstanceForSession();
	void StartSolverFrame(const int32 InSolverGUID, FChaosVDSolverFrameData&& FrameData);
	void CommitProcessedGameFramesToRecording();
	void StartGameFrame(const TSharedPtr<FChaosVDGameFrameData>& InFrameData);

	FChaosVDSolverFrameData* GetCurrentSolverFrame(const int32 InSolverGUID);
	
	TWeakPtr<FChaosVDGameFrameData> GetCurrentGameFrame();

	FChaosVDBinaryDataContainer& FindOrAddUnprocessedData(const int32 DataID);

	bool ProcessBinaryData(const int32 DataID);

	TSharedPtr<FChaosVDRecording> GetRecordingForSession() const;

	void RegisterDataProcessor(TSharedPtr<FChaosVDDataProcessorBase> InDataProcessor);

	void HandleAnalysisComplete();

	TMap<int32,int32>& GetCurrentTickOffsetsBySolverID() { return CurrentNetworkTickOffsets; };


	FChaosVDStepData* GetCurrentSolverStageDataForCurrentFrame(int32 SolverID, EChaosVDSolverStageAccessorFlags Flags);

private:

	void RegisterDefaultDataProcessorsIfNeeded();

	void EnqueueGameFrameForProcessing(const TSharedPtr<FChaosVDGameFrameData>& FrameData);
	void DeQueueGameFrameForProcessing(TSharedPtr<FChaosVDGameFrameData>& OutFrameData);

	/** Gathers any solver id from solver data that is not fully processed yet but that will be valid for the provided game frame data later on */
	void GetAvailablePendingSolverIDsAtGameFrame(const TSharedRef<FChaosVDGameFrameData>& InProcessedGameFrameData, TArray<int32, TInlineAllocator<16>>& OutSolverIDs);
	
	TraceServices::IAnalysisSession& Session;

	TSharedPtr<FChaosVDRecording> InternalRecording;

	TMap<int32, TSharedPtr<FChaosVDBinaryDataContainer>> UnprocessedDataByID;

	TMap<FStringView, TSharedPtr<FChaosVDDataProcessorBase>> RegisteredDataProcessors;

	TMap<int32, FChaosVDSolverFrameData> CurrentSolverFramesByID;

	TQueue<TSharedPtr<FChaosVDGameFrameData>> CurrentGameFrameQueue;

	TWeakPtr<FChaosVDGameFrameData> CurrentGameFrame = nullptr;

	Chaos::VisualDebugger::FChaosVDArchiveHeader DefaultHeaderData;

	int32 CurrentGameFrameQueueSize = 0;

	bool bDefaultDataProcessorsRegistered = false;

	double StartLastCommitedFrameTimeSeconds = 0.0;

	TMap<int32, int32> CurrentNetworkTickOffsets;
};
