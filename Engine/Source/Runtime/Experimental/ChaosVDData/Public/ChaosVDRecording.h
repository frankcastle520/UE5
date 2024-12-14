// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Core.h"
#include "Containers/UnrealString.h"
#include "DataWrappers/ChaosVDCollisionDataWrappers.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "UObject/ObjectMacros.h"
#include "Containers/UnrealString.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/ImplicitObject.h"
#include "ChaosVisualDebugger/ChaosVDMemWriterReader.h"
#include "DataWrappers/ChaosVDCharacterGroundConstraintDataWrappers.h"
#include "DataWrappers/ChaosVDJointDataWrappers.h"
#include "DataWrappers/ChaosVDQueryDataWrappers.h"
#include <atomic>

#include "DataWrappers/ChaosVDAccelerationStructureDataWrappers.h"
#include "DataWrappers/ChaosVDDebugShapeDataWrapper.h"

namespace Chaos::VisualDebugger
{
	class FChaosVDSerializableNameTable;
}

DECLARE_MULTICAST_DELEGATE_TwoParams(FChaosVDGeometryDataLoaded, const Chaos::FConstImplicitObjectPtr&, const uint32 GeometryID)

/** Set of flags used to define characteristics of a loaded solver stage */
enum class EChaosVDSolverStageFlags : uint8
{
	None = 0,
	/** Set if the solver stage is open and can take new data */
	Open = 1 << 0,
	/** Set if the solver stage was explicitly recorded - If not set, this stage was created on the fly during load */
	ExplicitStage = 1 << 1,
};

ENUM_CLASS_FLAGS(EChaosVDSolverStageFlags)

struct FChaosVDStepData
{
	FString StepName;
	TArray<TSharedPtr<FChaosVDParticleDataWrapper>> RecordedParticlesData;
	TArray<TSharedPtr<FChaosVDParticlePairMidPhase>> RecordedMidPhases;
	TArray<TSharedPtr<FChaosVDJointConstraint>> RecordedJointConstraints;
	TArray<FChaosVDConstraint> RecordedConstraints;
	TMap<int32, TArray<FChaosVDConstraint>> RecordedConstraintsByParticleID;
	TMap<int32, TArray<TSharedPtr<FChaosVDParticlePairMidPhase>>> RecordedMidPhasesByParticleID;
	TSet<int32> ParticlesDestroyedIDs;

	EChaosVDSolverStageFlags StageFlags = EChaosVDSolverStageFlags::None;
};

struct FChaosVDTrackedLocation
{
	FString DebugName;
	FVector Location;
};

struct FChaosVDTrackedTransform
{
	FString DebugName;
	FTransform Transform;
};

enum class EChaosVDNetworkSyncDataRequirements
{
	None = 0,
	InternalFrameNumber  = 1 << 0,
	NetworkTickOffset = 1 << 1,

	All = InternalFrameNumber | NetworkTickOffset
};

ENUM_CLASS_FLAGS(EChaosVDNetworkSyncDataRequirements)

typedef TArray<FChaosVDStepData, TInlineAllocator<16>> FChaosVDStepsContainer;

struct CHAOSVDDATA_API FChaosVDSolverFrameData
{

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FChaosVDSolverFrameData() = default;
	FChaosVDSolverFrameData(const FChaosVDSolverFrameData& Other) = default;
	FChaosVDSolverFrameData(FChaosVDSolverFrameData&& Other) noexcept = default;
	FChaosVDSolverFrameData& operator=(const FChaosVDSolverFrameData& Other) = default;
	FChaosVDSolverFrameData& operator=(FChaosVDSolverFrameData&& Other) noexcept = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	FName DebugFName;
	UE_DEPRECATED(5.5, "Please use the DebugFName instead")
	FString DebugName;
	int32 SolverID = INDEX_NONE;
	int32 InternalFrameNumber = INDEX_NONE;
	int32 NetworkTickOffset = INDEX_NONE;
	uint64 FrameCycle = 0;
	Chaos::FRigidTransform3 SimulationTransform;
	bool bIsKeyFrame = false;
	bool bIsResimulated = false;
	FChaosVDStepsContainer SolverSteps;
	TSet<int32> ParticlesDestroyedIDs;
	double StartTime = -1.0;
	double EndTime = -1.0;
	TArray<TSharedPtr<FChaosVDCharacterGroundConstraint>> RecordedCharacterGroundConstraints;
	
	/** Calculates and returns the frame time for this recorded frame.
	 * @return Calculated frame time. -1 if it was not recorded
	 */
	double GetFrameTime() const
	{
		if (StartTime < 0 || EndTime < 0)
		{
			return -1.0;
		}

		return EndTime - StartTime;
	}

	/** Returns true if we have the necessary data to sync this frame with other frame based on network ticks offsets */
	bool HasNetworkSyncData(EChaosVDNetworkSyncDataRequirements Requirements = EChaosVDNetworkSyncDataRequirements::All) const
	{
		bool bHasRequiredSyncData = true;
		if (EnumHasAnyFlags(Requirements, EChaosVDNetworkSyncDataRequirements::InternalFrameNumber))
		{
			bHasRequiredSyncData &= InternalFrameNumber != INDEX_NONE;
		}
		
		if (EnumHasAnyFlags(Requirements, EChaosVDNetworkSyncDataRequirements::NetworkTickOffset))
		{
			bHasRequiredSyncData &= NetworkTickOffset != INDEX_NONE;
		}

		return bHasRequiredSyncData;
	}

	/** Returns the current network tick offset. If we didn't have recorded a network tick, we will still return 0 to keep compatibility with other files
	 */
	int32 GetClampedNetworkTickOffset() const
	{
		return NetworkTickOffset >= 0 ? NetworkTickOffset : 0;
	}
};

struct FChaosVDGameFrameData
{

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FChaosVDGameFrameData() = default;
	FChaosVDGameFrameData(const FChaosVDGameFrameData& Other) = default;
	FChaosVDGameFrameData(FChaosVDGameFrameData&& Other) noexcept = default;
	FChaosVDGameFrameData& operator=(const FChaosVDGameFrameData& Other) = default;
	FChaosVDGameFrameData& operator=(FChaosVDGameFrameData&& Other) noexcept = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	uint64 FirstCycle;
	uint64 LastCycle;
	double StartTime = -1.0;
	double EndTime = -1.0;

	/** Calculates and returns the frame time for this recorded frame.
	 * @return Calculated frame time. -1 if it was not recorded
	 */
	double GetFrameTime() const
	{
		if (StartTime < 0 || EndTime < 0)
		{
			return -1.0;
		}

		return EndTime - StartTime;
	}

	bool IsDirty() const { return bIsDirty; };

	void MarkDirty() { bIsDirty = true; }

	TMap<FName, FChaosVDTrackedLocation> RecordedNonSolverLocationsByID;
	TMap<FName, FChaosVDTrackedTransform> RecordedNonSolverTransformsByID;
	TMap<int32, TMap<int32, TSharedPtr<FChaosVDQueryDataWrapper>>> RecordedSceneQueriesBySolverID;

	UE_DEPRECATED(5.5, "RecordedSceneQueries is deprecated and will be removed in a future release, use RecordedSceneQueriesByQueryID instead.")
	TMap<int32, TSharedPtr<FChaosVDQueryDataWrapper>> RecordedSceneQueries;

	TMap<int32, TSharedPtr<FChaosVDQueryDataWrapper>> RecordedSceneQueriesByQueryID;
	TMap<int32, TArray<TSharedPtr<FChaosVDAABBTreeDataWrapper>>> RecordedAABBTreesBySolverID;

	TMap<int32,TArray<TSharedPtr<FChaosVDDebugDrawBoxDataWrapper>>> RecordedDebugDrawBoxesBySolverID;
	TMap<int32,TArray<TSharedPtr<FChaosVDDebugDrawLineDataWrapper>>> RecordedDebugDrawLinesBySolverID;
	TMap<int32,TArray<TSharedPtr<FChaosVDDebugDrawSphereDataWrapper>>> RecordedDebugDrawSpheresBySolverID;
	TMap<int32,TArray<TSharedPtr<FChaosVDDebugDrawImplicitObjectDataWrapper>>> RecordedDebugDrawImplicitObjectsBySolverID;

private:
	bool bIsDirty = false;
};

/**
 * Struct that represents a recorded Physics simulation.
 * It is currently populated while analyzing a Trace session
 */
struct CHAOSVDDATA_API FChaosVDRecording
{
	FChaosVDRecording();

	/* Constant used to define inline allocators -
	* Unless there are some scenarios with a lot of RBAN solvers in the recording, we usually don't go over 3 tracks most of the time so 16 should be plenty by default */
	static constexpr int32 CommonTrackCount = 16;

	/** Returns the current available recorded solvers number */
	int32 GetAvailableSolversNumber_AssumesLocked() const { return RecordedFramesDataPerSolver.Num(); }
	
	/** Returns the current available Game Frames */
	int32 GetAvailableGameFramesNumber() const;
	int32 GetAvailableGameFramesNumber_AssumesLocked() const;

	/** Returns a reference to the array holding all the available game frames */
	const TArray<FChaosVDGameFrameData>& GetAvailableGameFrames_AssumesLocked() const { return GameFrames; }

	/** Returns a reference to the map containing the available solver data */
	const TMap<int32, TArray<FChaosVDSolverFrameData>>& GetAvailableSolvers_AssumesLocked() const { return RecordedFramesDataPerSolver; }

	/**
	 * Returns the number of available frame data for the specified solver ID
	 * @param SolverID ID of the solver 
	 */
	int32 GetAvailableSolverFramesNumber(int32 SolverID) const;
	int32 GetAvailableSolverFramesNumber_AssumesLocked(int32 SolverID) const;
	
	/**
	 * Returns the name of the specified solver id
	 * @param SolverID ID of the solver 
	 */
	FName GetSolverFName(int32 SolverID);

	UE_DEPRECATED(5.5, "Please use the GetSolverFName instead")
	FString GetSolverName(int32 SolverID) { return TEXT(""); }

	/**
	 * Returns the name of the specified solver id. Must be called from within a ReadLock
	 * @param SolverID ID of the solver
	 */
	FName GetSolverFName_AssumedLocked(int32 SolverID);

	bool IsServerSolver_AssumesLocked(int32 SolverID);
	bool IsServerSolver(int32 SolverID);

	UE_DEPRECATED(5.5, "Please use the GetSolverFName_AssumedLocked instead")
	FString GetSolverName_AssumedLocked(int32 SolverID) { return TEXT(""); }

	/**
	 * Return a ptr to the existing solver frame data from the specified ID and Frame number
	 * @param SolverID ID of the solver
	 * @param FrameNumber Frame number
	 * @param bKeyFrameOnly True if we should return a keyframe (real or generated) for the provided frame number if available or nothing
	 * @return Ptr to the existing solver frame data from the specified ID and Frame number - It is a ptr to the array element, Do not store
	 */
	FChaosVDSolverFrameData* GetSolverFrameData_AssumesLocked(int32 SolverID, int32 FrameNumber, bool bKeyFrameOnly = false);
	
	/**
	 * Return a ptr to the existing solver frame data from the specified ID and Frame number
	 * @param SolverID ID if the solver
	 * @param Cycle Platform cycle at which the solver frame was recorded
	 * @return Ptr to the existing solver frame data from the specified ID and Frame number - It is a ptr to the array element, Do not store
	 */
	FChaosVDSolverFrameData* GetSolverFrameDataAtCycle_AssumesLocked(int32 SolverID, uint64 Cycle);

	/**
	 * Searches and returns the lowest frame number of a solver at the specified cycle
	 * @param SolverID ID if the solver
	 * @param Cycle Platform cycle to use as lower bound
	 * @return Found frame number. INDEX_NONE if no frame is found for the specified cycle
	 */
	int32 GetLowestSolverFrameNumberAtCycle(int32 SolverID, uint64 Cycle);
	int32 GetLowestSolverFrameNumberAtCycle_AssumesLocked(int32 SolverID, uint64 Cycle);

	int32 GetLowestSolverFrameNumberAtNetworkFrameNumber_AssumesLocked(int32 SolverID, int32 NetworkFrameNumber);

	int32 FindFirstSolverKeyFrameNumberFromFrame_AssumesLocked(int32 SolverID, int32 StartFrameNumber);
	
	/**
	 * Searches and returns the lowest frame number of a solver at the specified cycle
	 * @param SolverID ID if the solver
	 * @param GameFrame Platform cycle to use as lower bound
	 * @return Found frame number. INDEX_NONE if no frame is found for the specified cycle
	 */
	int32 GetLowestSolverFrameNumberGameFrame(int32 SolverID, int32 GameFrame);
	int32 GetLowestSolverFrameNumberGameFrame_AssumesLocked(int32 SolverID, int32 GameFrame);
	
	/**
	 * Searches and returns the lowest game frame number at the specified solver frame
	 * @param SolverID ID if the solver to evaluate
	 * @param SolverFrame Frame number of the solver to evaluate
	 * @return Found Game frame number. INDEX_NONE if no frame is found for the specified cycle
	 */
	int32 GetLowestGameFrameAtSolverFrameNumber(int32 SolverID, int32 SolverFrame);
	int32 GetLowestGameFrameAtSolverFrameNumber_AssumesLocked(int32 SolverID, int32 SolverFrame);

	/**
	 * Adds a Solver Frame Data entry for a specific Solver ID. Creates a solver entry if it does not exist 
	 * @param SolverID ID of the solver to add
	 * @param InFrameData Reference to the frame data we want to add
	 */
	void AddFrameForSolver(const int32 SolverID, FChaosVDSolverFrameData&& InFrameData);

	/**
	 * Adds a Game Frame Data entry. Creates a solver entry if it does not exist 
	 * @param InFrameData Reference to the frame data we want to add
	 */
	void AddGameFrameData(const FChaosVDGameFrameData& InFrameData);

	/** Called each time new geometry data becomes available in the recording - Mainly when a new frame is added from the Trace analysis */
	FChaosVDGeometryDataLoaded& OnGeometryDataLoaded() { return GeometryDataLoaded; };

	/**
	 * Searches for a recorded Game frame at the specified cycle 
	 * @param Cycle Platform Cycle to be used in the search
	 * @return A ptr to the recorded game frame data - This is a ptr to the array element. Do not store
	 */
	FChaosVDGameFrameData* GetGameFrameDataAtCycle_AssumesLocked(uint64 Cycle);

	/**
	 * Searches for a recorded Game frame at the specified cycle 
	 * @param FrameNumber Frame Number
	 * @return A ptr to the recorded game frame data - This is a ptr to the array element. Do not store
	 */
	FChaosVDGameFrameData* GetGameFrameData_AssumesLocked(int32 FrameNumber);

	/** Returns a ptr to the last recorded game frame - This is a ptr to the array element. Do not store */
	FChaosVDGameFrameData* GetLastGameFrameData_AssumesLocked();

	/**
	 * Searches and returns the lowest game frame number at the specified cycle
	 * @param Cycle Platform Cycle to be used in the search as lower bound
	 * @return Found Game frame number. INDEX_NONE if no frame is found for the specified cycle
	 */
	int32 GetLowestGameFrameNumberAtCycle(uint64 Cycle);

	/**
	 * Searches and returns the lowest game frame number at the specified cycle
	 * @param Time Platform Time to be used in the search as lower bound
	 * @return Found Game frame number. INDEX_NONE if no frame is found for the specified cycle
	 */
	int32 GetLowestGameFrameNumberAtTime(double Time);

	/**
     * Gathers all available solvers IDs at the given Game frame number
     * @param FrameNumber Game Frame number to evaluate
     * @param OutSolversID Solver's ID array to be filled with any IDs found
     */
	template<typename TAllocator>
	void GetAvailableSolverIDsAtGameFrameNumber(int32 FrameNumber,TArray<int32, TAllocator>& OutSolversID);
	template<typename TAllocator>
	void GetAvailableSolverIDsAtGameFrameNumber_AssumesLocked(int32 FrameNumber, TArray<int32, TAllocator>& OutSolversID);
	template<typename TAllocator>
	void GetAvailableSolverIDsAtGameFrame(const FChaosVDGameFrameData& GameFrameData, TArray<int32, TAllocator>& OutSolversID);
	template<typename TAllocator>
	void GetAvailableSolverIDsAtGameFrame_AssumesLocked(const FChaosVDGameFrameData& GameFrameData, TArray<int32, TAllocator>& OutSolversID);

	/** Collapses the most important frame data from a range of solver frames into a single solver frame data */
	void CollapseSolverFramesRange_AssumesLocked(int32 SolverID, int32 StartFrame, int32 EndFrame, FChaosVDSolverFrameData& OutCollapsedFrameData);

	/** Returns a reference to the GeometryID-ImplicitObject map of this recording */
	const TMap<uint32, Chaos::FConstImplicitObjectPtr>& GetGeometryMap() const { return ImplicitObjects; };

	UE_DEPRECATED(5.4, "Please use GetGeometryMap instead")
	const TMap<uint32, TSharedPtr<const Chaos::FImplicitObject>>& GetGeometryDataMap() const
	{
		check(false);
		static TMap<uint32, TSharedPtr<const Chaos::FImplicitObject>> DummyMap;
		return DummyMap;
	};

	/** Adds a shared Implicit Object to the recording */
	void AddImplicitObject(const uint32 ID, const Chaos::FImplicitObjectPtr& InImplicitObject);
	
	UE_DEPRECATED(5.4, "Please use AddImplicitObject with FImplicitObjectPtr instead")
	void AddImplicitObject(const uint32 ID, const TSharedPtr<Chaos::FImplicitObject>& InImplicitObject);

	/** Session name of the trace session used to re-build this recording */
	FString SessionName;

	FRWLock& GetRecordingDataLock() { return RecordingDataLock; }

	/** Returns true if this recording is being populated from a live session */
	bool IsLive() const { return bIsLive; }

	/** Sets if this recording is being populated from a live session */
	void SetIsLive(bool bNewIsLive) { bIsLive = bNewIsLive; }

	/** Returns the name table instances used to de-duplicate strings serialization */
	TSharedPtr<Chaos::VisualDebugger::FChaosVDSerializableNameTable> GetNameTableInstance() const { return NameTable; }

	/** Returns the FArchive header used to read the serialized binary data */
	const Chaos::VisualDebugger::FChaosVDArchiveHeader& GetHeaderData() const { return HeaderData; }
	
	/** Sets the FArchive header used to read the serialized binary data */
	void SetHeaderData(const Chaos::VisualDebugger::FChaosVDArchiveHeader& InNewHeader) { HeaderData = InNewHeader; }

	/** Returns true if this recording does not have any usable data */
	bool IsEmpty() const;

	/** Returns the last Platform Cycle on which this recording was updated (A new frame was added) */
	uint64 GetLastUpdatedTimeAsCycle() { return LastUpdatedTimeAsCycle; }

	TSharedPtr<FChaosVDCollisionChannelsInfoContainer> GetCollisionChannelsInfoContainer(){ return CollisionChannelsInfoContainer; }
	
	void SetCollisionChannelsInfoContainer(const TSharedPtr<FChaosVDCollisionChannelsInfoContainer>& InCollisionChannelsInfo);

protected:

	/** Adds an Implicit Object to the recording and takes ownership of it */
	void AddImplicitObject(const uint32 ID, const Chaos::FImplicitObject* InImplicitObject);
	
	void AddImplicitObject_Internal(const uint32 ID, const Chaos::FConstImplicitObjectPtr& InImplicitObject);

	/** Stores a frame number of a solver that is a Key Frame -
	 * These are used when scrubbing to make sure the visualization is in sync with what was recorded
	 */
	void AddKeyFrameNumberForSolver(int32 SolverID, int32 FrameNumber);
	void AddKeyFrameNumberForSolver_AssumesLocked(int32 SolverID, int32 FrameNumber);
	void GenerateAndStoreKeyframeForSolver_AssumesLocked(int32 SolverID, int32 CurrentFrameNumber, int32 LastKeyFrameNumber);

	TMap<int32, TArray<FChaosVDSolverFrameData>> RecordedFramesDataPerSolver;
	TMap<int32, TMap<int32, FChaosVDSolverFrameData>> GeneratedKeyFrameDataPerSolver;
	TMap<int32, TArray<int32>> RecordedKeyFramesNumberPerSolver;
	TArray<FChaosVDGameFrameData> GameFrames;

	FChaosVDGeometryDataLoaded GeometryDataLoaded;

	/** Id to Ptr map of all shared geometry data required to visualize */
	TMap<uint32, Chaos::FConstImplicitObjectPtr> ImplicitObjects;

	TSharedPtr<Chaos::VisualDebugger::FChaosVDSerializableNameTable> NameTable;

	mutable FRWLock RecordingDataLock;

	/** True if this recording is being populated from a live session */
	bool bIsLive = false;

	/** Last Platform Cycle on which this recording was updated */
	std::atomic<uint64> LastUpdatedTimeAsCycle;

	/** Map that temporary holds generated particle data during the key frame generation process, keeping its memory allocation between generated frames*/
	TMap<int32, TSharedPtr<FChaosVDParticleDataWrapper>> ParticlesOnCurrentGeneratedKeyframe;

	Chaos::VisualDebugger::FChaosVDArchiveHeader HeaderData;

	TSharedPtr<FChaosVDCollisionChannelsInfoContainer> CollisionChannelsInfoContainer;

	friend class FChaosVDTraceProvider;
	friend class FChaosVDTraceImplicitObjectProcessor;
};

template <typename TAllocator>
void FChaosVDRecording::GetAvailableSolverIDsAtGameFrameNumber(int32 FrameNumber, TArray<int32, TAllocator>& OutSolversID)
{
	FReadScopeLock ReadLock(RecordingDataLock);
	return GetAvailableSolverIDsAtGameFrameNumber_AssumesLocked(FrameNumber, OutSolversID);
}

template <typename TAllocator>
void FChaosVDRecording::GetAvailableSolverIDsAtGameFrameNumber_AssumesLocked(int32 FrameNumber, TArray<int32, TAllocator>& OutSolversID)
{
	if (!GameFrames.IsValidIndex(FrameNumber))
	{
		return;
	}
	
	GetAvailableSolverIDsAtGameFrame_AssumesLocked(GameFrames[FrameNumber], OutSolversID);
}

template <typename TAllocator>
void FChaosVDRecording::GetAvailableSolverIDsAtGameFrame(const FChaosVDGameFrameData& GameFrameData, TArray<int32, TAllocator>& OutSolversID)
{
	FReadScopeLock ReadLock(RecordingDataLock);
	GetAvailableSolverIDsAtGameFrame_AssumesLocked(GameFrameData, OutSolversID);
}

template <typename TAllocator>
void FChaosVDRecording::GetAvailableSolverIDsAtGameFrame_AssumesLocked(const FChaosVDGameFrameData& GameFrameData, TArray<int32, TAllocator>& OutSolversID)
{
	OutSolversID.Reserve(RecordedFramesDataPerSolver.Num());

	for (const TPair<int32, TArray<FChaosVDSolverFrameData>>& SolverFramesWithIDPair : RecordedFramesDataPerSolver)
	{
		if (SolverFramesWithIDPair.Value.IsEmpty())
		{
			continue;
		}

		if (SolverFramesWithIDPair.Value.Num() == 1 && SolverFramesWithIDPair.Value[0].FrameCycle < GameFrameData.FirstCycle)
		{
			OutSolversID.Add(SolverFramesWithIDPair.Key);
		}
		else
		{
			if (GameFrameData.FirstCycle > SolverFramesWithIDPair.Value[0].FrameCycle && GameFrameData.FirstCycle < SolverFramesWithIDPair.Value.Last().FrameCycle)
			{
				OutSolversID.Add(SolverFramesWithIDPair.Key);
			}
		}	
	}
}
