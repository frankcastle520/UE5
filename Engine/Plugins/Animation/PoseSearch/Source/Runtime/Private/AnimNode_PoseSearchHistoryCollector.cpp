// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeMessages.h"
#include "Animation/AnimStats.h"
#include "Engine/SkinnedAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_PoseSearchHistoryCollector)

#define LOCTEXT_NAMESPACE "AnimNode_PoseSearchHistoryCollector"

/////////////////////////////////////////////////////
// FAnimNode_PoseSearchHistoryCollector_Base

void FAnimNode_PoseSearchHistoryCollector_Base::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);

	PoseHistory.Initialize_AnyThread(PoseCount, SamplingInterval);

	if (bInitializeWithRefPose)
	{
		const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();
		if (BoneContainer.IsValid())
		{
			// initializing PoseHistory with a ref pose at FAnimInstanceProxy location/facing
			FMemMark Mark(FMemStack::Get());
			FCSPose<FCompactPose> ComponentSpacePose;
			FBlendedCurve EmptyCurves;
			ComponentSpacePose.InitPose(&BoneContainer);
			PoseHistory.EvaluateComponentSpace_AnyThread(0.f, ComponentSpacePose, bStoreScales,
				RootBoneRecoveryTime, RootBoneTranslationRecoveryRatio, RootBoneRotationRecoveryRatio, true, true, 
				GetRequiredBones(Context.AnimInstanceProxy), EmptyCurves, MakeConstArrayView(CollectedCurves));
		}
	}
}

TArray<FBoneIndexType> FAnimNode_PoseSearchHistoryCollector_Base::GetRequiredBones(const FAnimInstanceProxy* AnimInstanceProxy) const
{
	check(AnimInstanceProxy);

	TArray<FBoneIndexType> RequiredBones;
	if (!CollectedBones.IsEmpty())
	{
		if (const USkeletalMeshComponent* SkeletalMeshComponent = AnimInstanceProxy->GetSkelMeshComponent())
		{
			if (const USkinnedAsset* SkinnedAsset = SkeletalMeshComponent->GetSkinnedAsset())
			{
				if (const USkeleton* Skeleton = SkinnedAsset->GetSkeleton())
				{
					RequiredBones.Reserve(CollectedBones.Num());
					for (const FBoneReference& BoneReference : CollectedBones)
					{
						FBoneReference BoneReferenceCopy = BoneReference;
						if (BoneReferenceCopy.Initialize(Skeleton))
						{
							RequiredBones.AddUnique(BoneReferenceCopy.BoneIndex);
						}
					}
				}
			}
		}
	}

	return RequiredBones;
}

void FAnimNode_PoseSearchHistoryCollector_Base::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);
	check(Context.AnimInstanceProxy);

	Super::CacheBones_AnyThread(Context);

	bCacheBones = true;
}

void FAnimNode_PoseSearchHistoryCollector_Base::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);

	PoseHistory.SetTrajectory(bGenerateTrajectory ? FPoseSearchQueryTrajectory() : Trajectory, TrajectorySpeedMultiplier);

	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());
}

void FAnimNode_PoseSearchHistoryCollector_Base::PreUpdate(const UAnimInstance* InAnimInstance)
{
	Super::PreUpdate(InAnimInstance);
	
	if (bGenerateTrajectory)
	{
		GenerateTrajectory(InAnimInstance);
	}

	PoseHistory.PreUpdate();

	bIsTrajectoryGeneratedBeforePreUpdate = false;
}

void FAnimNode_PoseSearchHistoryCollector_Base::GenerateTrajectory(const UAnimInstance* InAnimInstance)
{
	if (!bIsTrajectoryGeneratedBeforePreUpdate)
	{
		FPoseSearchTrajectoryData::FSampling TrajectoryDataSampling;
		TrajectoryDataSampling.NumHistorySamples = FMath::Max(PoseCount, TrajectoryHistoryCount);
		TrajectoryDataSampling.SecondsPerHistorySample = SamplingInterval;
		TrajectoryDataSampling.NumPredictionSamples = TrajectoryPredictionCount;
		TrajectoryDataSampling.SecondsPerPredictionSample = PredictionSamplingInterval;

		PoseHistory.GenerateTrajectory(InAnimInstance, InAnimInstance->GetDeltaSeconds(), TrajectoryData, TrajectoryDataSampling);

		bIsTrajectoryGeneratedBeforePreUpdate = true;
	}
}
/////////////////////////////////////////////////////
// FAnimNode_PoseSearchHistoryCollector

void FAnimNode_PoseSearchHistoryCollector::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);
	Super::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_PoseSearchHistoryCollector::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Super::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);
}

void FAnimNode_PoseSearchHistoryCollector::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);
	ANIM_MT_SCOPE_CYCLE_COUNTER_VERBOSE(PoseSearchHistoryCollector, !IsInGameThread());

	check(Output.AnimInstanceProxy);

	Super::Evaluate_AnyThread(Output);
	Source.Evaluate(Output);

	const bool bNeedsReset = bResetOnBecomingRelevant && UpdateCounter.HasEverBeenUpdated() && !UpdateCounter.WasSynchronizedCounter(Output.AnimInstanceProxy->GetUpdateCounter());

	FCSPose<FCompactPose> ComponentSpacePose;
	ComponentSpacePose.InitPose(Output.Pose);

	TArray<FBoneIndexType> RequiredBones;
	if (bCacheBones)
	{
		RequiredBones = GetRequiredBones(Output.AnimInstanceProxy);
	}

	PoseHistory.EvaluateComponentSpace_AnyThread(Output.AnimInstanceProxy->GetDeltaSeconds(), ComponentSpacePose, bStoreScales,
		RootBoneRecoveryTime, RootBoneTranslationRecoveryRatio, RootBoneRotationRecoveryRatio, bNeedsReset, bCacheBones, 
		RequiredBones, Output.Curve, MakeConstArrayView(CollectedCurves));

	bCacheBones = false;

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	FColor Color;
#if WITH_EDITORONLY_DATA
	Color = DebugColor.ToFColor(true);
#else // WITH_EDITORONLY_DATA
	Color = FLinearColor::Red.ToFColor(true);
#endif // WITH_EDITORONLY_DATA
	PoseHistory.DebugDraw(*Output.AnimInstanceProxy, Color);
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
}

void FAnimNode_PoseSearchHistoryCollector::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);
	Super::Update_AnyThread(Context);
	UE::Anim::TScopedGraphMessage<UE::PoseSearch::FPoseHistoryProvider> ScopedMessage(Context, this);
	Source.Update(Context);
}

void FAnimNode_PoseSearchHistoryCollector::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);
	Source.GatherDebugData(DebugData);
}

/////////////////////////////////////////////////////
// FAnimNode_PoseSearchComponentSpaceHistoryCollector

void FAnimNode_PoseSearchComponentSpaceHistoryCollector::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);
	Super::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_PoseSearchComponentSpaceHistoryCollector::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Super::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);
}

void FAnimNode_PoseSearchComponentSpaceHistoryCollector::EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateComponentSpace_AnyThread);
	ANIM_MT_SCOPE_CYCLE_COUNTER_VERBOSE(PoseSearchComponentSpaceHistoryCollector, !IsInGameThread());

	check(Output.AnimInstanceProxy);

	Super::EvaluateComponentSpace_AnyThread(Output);
	Source.EvaluateComponentSpace(Output);

	const bool bNeedsReset = bResetOnBecomingRelevant && UpdateCounter.HasEverBeenUpdated() && !UpdateCounter.WasSynchronizedCounter(Output.AnimInstanceProxy->GetUpdateCounter());

	TArray<FBoneIndexType> RequiredBones;
	if (bCacheBones)
	{
		RequiredBones = GetRequiredBones(Output.AnimInstanceProxy);
	}

	PoseHistory.EvaluateComponentSpace_AnyThread(Output.AnimInstanceProxy->GetDeltaSeconds(), Output.Pose, bStoreScales, 
		RootBoneRecoveryTime, RootBoneTranslationRecoveryRatio, RootBoneRotationRecoveryRatio, bNeedsReset, bCacheBones, RequiredBones);
	
	bCacheBones = false;

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	FColor Color;
#if WITH_EDITORONLY_DATA
	Color = DebugColor.ToFColor(true);
#else // WITH_EDITORONLY_DATA
	Color = FLinearColor::Red.ToFColor(true);
#endif // WITH_EDITORONLY_DATA
	PoseHistory.DebugDraw(*Output.AnimInstanceProxy, Color);
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
}

void FAnimNode_PoseSearchComponentSpaceHistoryCollector::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);
	Super::Update_AnyThread(Context);
	UE::Anim::TScopedGraphMessage<UE::PoseSearch::FPoseHistoryProvider> ScopedMessage(Context, this);
	Source.Update(Context);
}

void FAnimNode_PoseSearchComponentSpaceHistoryCollector::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);
	Source.GatherDebugData(DebugData);
}

#undef LOCTEXT_NAMESPACE
