// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BonePose.h"
#include "Containers/RingBuffer.h"
#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseHistoryProvider.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "UObject/ObjectKey.h"

struct FAnimInstanceProxy;
class USkeleton;
class UWorld;

namespace UE::PoseSearch
{

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
extern POSESEARCH_API TAutoConsoleVariable<bool> CVarAnimPoseHistoryDebugDrawPose;
extern POSESEARCH_API TAutoConsoleVariable<bool> CVarAnimPoseHistoryDebugDrawTrajectory;
#endif

struct FSearchResult;
typedef uint16 FComponentSpaceTransformIndex;
typedef TPair<FBoneIndexType, FComponentSpaceTransformIndex> FBoneToTransformPair;
typedef TMap<FBoneIndexType, FComponentSpaceTransformIndex> FBoneToTransformMap;

struct FPoseHistoryEntry
{
	// collected bones transforms in component space
	TArray<FQuat4f> ComponentSpaceRotations;
	TArray<FVector> ComponentSpacePositions;
	TArray<FVector3f> ComponentSpaceScales;
	TArray<float> CurveValues;
	float AccumulatedSeconds = 0.f;

	void Update(float Time, FCSPose<FCompactPose>& ComponentSpacePose, const FBoneToTransformMap& BoneToTransformMap, bool bStoreScales, const FBlendedCurve& Curves, const TConstArrayView<FName>& CollectedCurves);

	POSESEARCH_API void SetNum(int32 Num, bool bStoreScales);
	int32 Num() const;

	POSESEARCH_API void SetComponentSpaceTransform(int32 Index, const FTransform& Transform);
	FTransform GetComponentSpaceTransform(int32 Index) const;
	float GetCurveValue(int32 Index) const;
};
FArchive& operator<<(FArchive& Ar, FPoseHistoryEntry& Entry);

struct POSESEARCH_API IPoseHistory
{
public:
	virtual ~IPoseHistory() {}
	
	// returns the BoneIndexType transform relative to ReferenceBoneIndexType: 
	// if ReferenceBoneIndexType is 0 (RootBoneIndexType), OutBoneTransform is in root bone space
	// if ReferenceBoneIndexType is FBoneIndexType(-1) (ComponentSpaceIndexType), OutBoneTransform is in component space
	// if ReferenceBoneIndexType is FBoneIndexType(-2) (WorldSpaceIndexType), OutBoneTransform is in world space
	virtual bool GetTransformAtTime(float Time, FTransform& OutBoneTransform, const USkeleton* BoneIndexSkeleton = nullptr, FBoneIndexType BoneIndexType = RootBoneIndexType, FBoneIndexType ReferenceBoneIndexType = ComponentSpaceIndexType, bool bExtrapolate = false) const = 0;
	// @todo: consider consolidating into a (templated?) get X value at time once we add custom attributes to pose history.
	virtual bool GetCurveValueAtTime(float Time, const FName& CurveName, float& OutCurveValue, bool bExtrapolate = false) const = 0;
	virtual const FPoseSearchQueryTrajectory& GetTrajectory() const = 0;
	
	// @todo: deprecate this API. TrajectorySpeedMultiplier should be a global query scaling value passed as input parameter of FSearchContext during config BuildQuery
	virtual float GetTrajectorySpeedMultiplier() const = 0;
	virtual bool IsEmpty() const = 0;

	virtual const FBoneToTransformMap& GetBoneToTransformMap() const = 0;
	virtual const TConstArrayView<FName> GetCollectedCurves() const = 0;
	virtual int32 GetNumEntries() const = 0;
	virtual const FPoseHistoryEntry& GetEntry(int32 EntryIndex) const = 0;

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	virtual void DebugDraw(const UWorld* World, FColor Color) const = 0;
	virtual void DebugDraw(FAnimInstanceProxy& AnimInstanceProxy, FColor Color) const = 0;
	virtual void DebugDraw(FAnimInstanceProxy& AnimInstanceProxy, FColor Color, float Time, float PointSize = 6.f, bool bExtrapolate = false) const;
#endif
};

struct POSESEARCH_API FArchivedPoseHistory : public IPoseHistory
{
	void InitFrom(const IPoseHistory* PoseHistory);

	// IPoseHistory interface
	virtual bool GetTransformAtTime(float Time, FTransform& OutBoneTransform, const USkeleton* BoneIndexSkeleton = nullptr, FBoneIndexType BoneIndexType = RootBoneIndexType, FBoneIndexType ReferenceBoneIndexType = ComponentSpaceIndexType, bool bExtrapolate = false) const override;
	virtual bool GetCurveValueAtTime(float Time, const FName& CurveName, float& outcurvevalue, bool bExtrapolate = false) const override;
	virtual const FPoseSearchQueryTrajectory& GetTrajectory() const override { return Trajectory; }
	virtual float GetTrajectorySpeedMultiplier() const override { return 1.f; }
	virtual bool IsEmpty() const override { return Entries.IsEmpty(); }
	virtual const FBoneToTransformMap& GetBoneToTransformMap() const override { return BoneToTransformMap; }
	virtual const TConstArrayView<FName> GetCollectedCurves() const override { return CollectedCurves; }
	virtual int32 GetNumEntries() const override { return Entries.Num(); }
	virtual const FPoseHistoryEntry& GetEntry(int32 EntryIndex) const override { return Entries[EntryIndex]; }

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	virtual void DebugDraw(const UWorld* World, FColor Color) const override;
	virtual void DebugDraw(FAnimInstanceProxy& AnimInstanceProxy, FColor Color) const override { unimplemented(); }
#endif
	// End of IPoseHistory interface

	FBoneToTransformMap BoneToTransformMap;
	// @todo: Make this a map if this is expected to be big.
	TArray<FName> CollectedCurves;
	TArray<FPoseHistoryEntry> Entries;
	FPoseSearchQueryTrajectory Trajectory;
};
FArchive& operator<<(FArchive& Ar, FArchivedPoseHistory& Entry);

struct POSESEARCH_API FPoseHistory : public IPoseHistory
{
	FPoseHistory() = default;
	FPoseHistory(const FPoseHistory& Other);
	FPoseHistory(FPoseHistory&& Other);
	FPoseHistory& operator=(const FPoseHistory& Other);
	FPoseHistory& operator=(FPoseHistory&& Other);

	void GenerateTrajectory(const UAnimInstance* AnimInstance, float DeltaTime, const FPoseSearchTrajectoryData& TrajectoryData, const FPoseSearchTrajectoryData::FSampling& TrajectoryDataSampling);
	void PreUpdate();

	void Initialize_AnyThread(int32 InNumPoses, float InSamplingInterval);
	void EvaluateComponentSpace_AnyThread(float DeltaTime, FCSPose<FCompactPose>& ComponentSpacePose, bool bStoreScales,
		float RootBoneRecoveryTime, float RootBoneTranslationRecoveryRatio, float RootBoneRotationRecoveryRatio,
		bool bNeedsReset, bool bCacheBones, const TArray<FBoneIndexType>& RequiredBones);
	void EvaluateComponentSpace_AnyThread(float DeltaTime, FCSPose<FCompactPose>& ComponentSpacePose, bool bStoreScales,
		float RootBoneRecoveryTime, float RootBoneTranslationRecoveryRatio, float RootBoneRotationRecoveryRatio,
		bool bNeedsReset, bool bCacheBones, const TArray<FBoneIndexType>& RequiredBones, const FBlendedCurve& Curves, const TConstArrayView<FName>& CollectedCurves);

	// IPoseHistory interface
	virtual bool GetTransformAtTime(float Time, FTransform& OutBoneTransform, const USkeleton* BoneIndexSkeleton = nullptr, FBoneIndexType BoneIndexType = RootBoneIndexType, FBoneIndexType ReferenceBoneIndexType = ComponentSpaceIndexType, bool bExtrapolate = false) const override;
	virtual bool GetCurveValueAtTime(float Time, const FName& CurveName, float& outcurvevalue, bool bExtrapolate = false) const override;
	virtual const FPoseSearchQueryTrajectory& GetTrajectory() const override;
	virtual float GetTrajectorySpeedMultiplier() const override;
	virtual bool IsEmpty() const override;
	virtual const FBoneToTransformMap& GetBoneToTransformMap() const override;
	virtual const TConstArrayView<FName> GetCollectedCurves() const override;
	
	void SetTrajectory(const FPoseSearchQueryTrajectory& InTrajectory, float InTrajectorySpeedMultiplier = 1.f);
	virtual int32 GetNumEntries() const override;
	virtual const FPoseHistoryEntry& GetEntry(int32 EntryIndex) const override;

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	virtual void DebugDraw(const UWorld* World, FColor Color) const override { unimplemented(); }
	virtual void DebugDraw(FAnimInstanceProxy& AnimInstanceProxy, FColor Color) const override;
#endif
	// End of IPoseHistory interface
	
	int32 GetMaxNumPoses() const { return MaxNumPoses; }
	float GetSamplingInterval() const { return SamplingInterval; }

private:

	// caching MaxNumPoses, since FData::Entries.Max() is a padded number
	int32 MaxNumPoses = 0;

	float SamplingInterval = 0.f;

	FPoseSearchQueryTrajectory Trajectory;
	FPoseSearchTrajectoryData::FState TrajectoryDataState;
	// @todo: deprecate this member and expose it via blue print logic or as global query scaling multiplier
	float TrajectorySpeedMultiplier = 1.f;

	struct FPoseData
	{
		// skeleton from the last Update, to keep tracking skeleton changes, and support compatible skeletons
		TWeakObjectPtr<const USkeleton> LastUpdateSkeleton;

		// map of FBoneIndexType(s) to collect. If Empty all the bones get collected
		FBoneToTransformMap BoneToTransformMap;
		
		// list of curves that we want to collect into our history.
		TArray<FName> CollectedCurves;

		// GetTypeHash for BoneToTransformMap
		uint32 BoneToTransformMapTypeHash = 0;

		// ring buffer of collected bones
		TRingBuffer<FPoseHistoryEntry> Entries;
	};

	
	typedef TStaticArray<FPoseData, 2> FDoubleBufferedPoseData;
	FDoubleBufferedPoseData DoubleBufferedPoseData;
	int32 ReadPoseDataIndex = 0;

	int32 GetWritePoseDataIndex() const { return (ReadPoseDataIndex + 1) % 2; }
	const FPoseData& GetReadPoseData() const { return DoubleBufferedPoseData[ReadPoseDataIndex]; }
	FPoseData& EditWritePoseData() { return DoubleBufferedPoseData[GetWritePoseDataIndex()]; }

#if ENABLE_ANIM_DEBUG
	
	// used to analyze thread safety
	mutable FThreadSafeCounter ReadPoseDataThreadSafeCounter = 0;
	mutable FThreadSafeCounter WritePoseDataThreadSafeCounter = 0;

#endif // ENABLE_ANIM_DEBUG
};

struct POSESEARCH_API FMemStackPoseHistory : public IPoseHistory
{
	void Init(const IPoseHistory* InPoseHistory);

	// IPoseHistory interface
	virtual bool GetTransformAtTime(float Time, FTransform& OutBoneTransform, const USkeleton* BoneIndexSkeleton = nullptr, FBoneIndexType BoneIndexType = RootBoneIndexType, FBoneIndexType ReferenceBoneIndexType = ComponentSpaceIndexType, bool bExtrapolate = false) const override;
	virtual bool GetCurveValueAtTime(float Time, const FName& CurveName, float& outcurvevalue, bool bExtrapolate = false) const override;
	virtual const FPoseSearchQueryTrajectory& GetTrajectory() const override { check(PoseHistory); return PoseHistory->GetTrajectory(); }
	virtual float GetTrajectorySpeedMultiplier() const override { check(PoseHistory); return PoseHistory->GetTrajectorySpeedMultiplier(); }
	virtual bool IsEmpty() const override { check(PoseHistory); return PoseHistory->IsEmpty() && FutureEntries.IsEmpty(); }
	virtual const FBoneToTransformMap& GetBoneToTransformMap() const override { check(PoseHistory); return PoseHistory->GetBoneToTransformMap(); }
	virtual const TConstArrayView<FName> GetCollectedCurves() const override { check(PoseHistory); return PoseHistory->GetCollectedCurves(); }
	
	virtual int32 GetNumEntries() const override;
	virtual const FPoseHistoryEntry& GetEntry(int32 EntryIndex) const override;
	
#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	virtual void DebugDraw(const UWorld* World, FColor Color) const override { unimplemented(); }
	virtual void DebugDraw(FAnimInstanceProxy& AnimInstanceProxy, FColor Color) const override;
#endif
	// End of IPoseHistory interface

	void AddFutureRootBone(float Time, const FTransform& FutureRootBoneTransform, bool bStoreScales);
	void AddFuturePose(float Time, FCSPose<FCompactPose>& ComponentSpacePose);
	void AddFuturePose(float Time, FCSPose<FCompactPose>& ComponentSpacePose, const FBlendedCurve& Curves);

	const IPoseHistory* GetThisOrPoseHistory() const { return FutureEntries.IsEmpty() ? PoseHistory : this; }

private:
	const IPoseHistory* PoseHistory = nullptr;
	TArray<FPoseHistoryEntry, TInlineAllocator<4, TMemStackAllocator<>>> FutureEntries;
};

struct FHistoricalPoseIndex
{
	bool operator==(const FHistoricalPoseIndex& Index) const
	{
		return PoseIndex == Index.PoseIndex && DatabaseKey == Index.DatabaseKey;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FHistoricalPoseIndex& Index)
	{
		return HashCombineFast(::GetTypeHash(Index.PoseIndex), GetTypeHash(Index.DatabaseKey));
	}

	int32 PoseIndex = INDEX_NONE;
	FObjectKey DatabaseKey;
};

struct FPoseIndicesHistory
{
	void Update(const FSearchResult& SearchResult, float DeltaTime, float MaxTime);
	void Reset() { IndexToTime.Reset(); }
	TMap<FHistoricalPoseIndex, float> IndexToTime;
};

} // namespace UE::PoseSearch


