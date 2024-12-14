// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_MotionMatchingInteraction.h"
#include "Animation/AnimInertializationSyncScope.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimRootMotionProvider.h"
#include "PoseSearch/PoseSearchInteractionAsset.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/PoseHistoryProvider.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "HAL/IConsoleManager.h"

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<bool> CVarAnimNodeMotionMatchingInteractionDebug(TEXT("a.AnimNode.MotionMatchingInteraction.Debug"), false, TEXT("Turn on visualization debugging for AnimNode Motion Matching Interaction"));
#endif // ENABLE_ANIM_DEBUG

void FAnimNode_MotionMatchingInteraction::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);

	FString DebugLine = DebugData.GetNodeName(this);
#if ENABLE_ANIM_DEBUG
	static bool bActive = true;
	DebugLine += FString::Printf(TEXT("\n - Active: (%s)"), bActive);
#endif
	DebugData.AddDebugItem(DebugLine);
}

void FAnimNode_MotionMatchingInteraction::Reset()
{
	Super::Reset();
	TranslationWarpLerp = 0.f;
	RotationWarpLerp = 0.f;
	InteractingRolesNum = 0;
}

void FAnimNode_MotionMatchingInteraction::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	using namespace UE::PoseSearch;

	if (NeedsReset(Context))
	{
		Reset();
	}

	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());

	GetEvaluateGraphExposedInputs().Execute(Context);

	bool bBlendToExecuted = ConditionalBlendTo(Context);

	int32 NewInteractingRolesNum = 0;
	const float DeltaTime = Context.GetDeltaTime();
	if (FPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<FPoseHistoryProvider>())
	{
		// we're NOT providing the UMultiAnimAsset here, relying on the interaction subsystem to resolve the ContinuingProperties properly
		FPoseSearchContinuingProperties ContinuingProperties;
		ContinuingProperties.PlayingAsset = GetAnimAsset();
		ContinuingProperties.PlayingAssetAccumulatedTime = GetAccumulatedTime();

		check(Context.AnimInstanceProxy);
		const FPoseSearchInteractionBlueprintResult Result = UPoseSearchInteractionLibrary::MotionMatchInteraction(Availabilities, Context.AnimInstanceProxy->GetAnimInstanceObject(), ContinuingProperties, PoseHistoryProvider->GetHistoryCollector(), bValidateResultAgainstAvailabilities);

		UAnimationAsset* RoledAnimAsset = nullptr;
		if (const UMultiAnimAsset* MultiAnimAsset = Cast<UMultiAnimAsset>(Result.SelectedAnimation))
		{
			check(Result.SelectedDatabase != nullptr);

			RoledAnimAsset = MultiAnimAsset->GetAnimationAsset(Result.Role);
			NewInteractingRolesNum = MultiAnimAsset->GetNumRoles();
		}
		else if (UAnimationAsset* SingleAnimAsset = Cast<UAnimationAsset>(Result.SelectedAnimation))
		{
			RoledAnimAsset = SingleAnimAsset;
			NewInteractingRolesNum = 1;
		}

		if (RoledAnimAsset)
		{
			bool bExecuteBlendTo = false;
			bool bUpdatePropertiesFromResult = false;
			if (InteractingRolesNum == 0 || AnimPlayers.IsEmpty())
			{
				bExecuteBlendTo = true;
				bUpdatePropertiesFromResult = true;
			}
			else if (EvaluationMode == EMotionMatchingInteractionEvaluationMode::ContinuousReselection)
			{
				const FBlendStackAnimPlayer& MainAnimPlayer = AnimPlayers[0];
				const UAnimationAsset* PlayingAnimAsset = MainAnimPlayer.GetAnimationAsset();

				if (RoledAnimAsset != PlayingAnimAsset ||
					Result.bIsMirrored != MainAnimPlayer.GetMirror() ||
					Result.BlendParameters != MainAnimPlayer.GetBlendParameters() ||
					!Result.bIsContinuingPoseSearch)
				{
					bExecuteBlendTo = true;
				}

				bUpdatePropertiesFromResult = true;
			}
			else if (RoledAnimAsset == AnimPlayers[0].GetAnimationAsset() && Result.bIsContinuingPoseSearch)
			{
				// we don't update FullAlignedActorRootBoneTransform since we're not planning to blend into the newly selected animation here
				bUpdatePropertiesFromResult = true;
			}

			if (bUpdatePropertiesFromResult)
			{
				FullAlignedActorRootBoneTransform = Result.FullAlignedActorRootBoneTransform;
				WantedPlayRate = Result.WantedPlayRate;
				BlendParameters = Result.BlendParameters;
			}

			if (bExecuteBlendTo)
			{
				const FPoseSearchRoledSkeleton* RoledSkeleton = Result.SelectedDatabase->Schema->GetRoledSkeleton(Result.Role);
				check(RoledSkeleton);

				BlendTo(Context, RoledAnimAsset, Result.SelectedTime, Result.bLoop, Result.bIsMirrored, RoledSkeleton->MirrorDataTable.Get(),
					BlendTime, BlendProfile, BlendOption, bUseInertialBlend, BlendParameters, WantedPlayRate);

				bBlendToExecuted = true;
			}
		}

		if (bUseAnimRootMotionProvider)
		{
			const IPoseHistory& PoseHistory = PoseHistoryProvider->GetPoseHistory();
			const USkeleton* Skeleton = Context.AnimInstanceProxy->GetSkeleton();
			FTransform RootBoneTrasform;
			PoseHistory.GetTransformAtTime(0.f, RootBoneTrasform, Skeleton, RootBoneIndexType, WorldSpaceIndexType);

			const FTransform LerpedAlignedActorRootBoneTransform(
				FQuat::Slerp(RootBoneTrasform.GetRotation(), FullAlignedActorRootBoneTransform.GetRotation(), RotationWarpLerp),
				FMath::Lerp(RootBoneTrasform.GetTranslation(), FullAlignedActorRootBoneTransform.GetTranslation(), TranslationWarpLerp),
				FVector::ZeroVector);

#if ENABLE_ANIM_DEBUG
			if (CVarAnimNodeMotionMatchingInteractionDebug.GetValueOnAnyThread())
			{
				Context.AnimInstanceProxy->AnimDrawDebugCoordinateSystem(RootBoneTrasform.GetLocation(), RootBoneTrasform.Rotator(), 25.f, false, 0.f, 0.f, SDPG_Foreground);
				Context.AnimInstanceProxy->AnimDrawDebugCoordinateSystem(FullAlignedActorRootBoneTransform.GetLocation(), FullAlignedActorRootBoneTransform.Rotator(), 50.f, false, 0.f, 0.f, SDPG_Foreground);
			}
#endif // ENABLE_ANIM_DEBUG

			WarpRootMotionTransform = LerpedAlignedActorRootBoneTransform.GetRelativeTransform(RootBoneTrasform);
			check(WarpRootMotionTransform.IsRotationNormalized());
		}
	}
	else
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FAnimNode_MotionMatchingInteraction::Update_AnyThread couldn't find the FPoseHistoryProvider"));
	}

	const bool bDidBlendToRequestAnInertialBlend = bBlendToExecuted && bUseInertialBlend;
	UE::Anim::TOptionalScopedGraphMessage<UE::Anim::FAnimInertializationSyncScope> InertializationSync(bDidBlendToRequestAnInertialBlend, Context);
	
	UpdatePlayRate(WantedPlayRate);
	UpdateBlendspaceParameters(BlendspaceUpdateMode, BlendParameters);

	// calculating the translation and rotation warp lerps, used to warp the root transform towards the last computed FullAlignedActorRootBoneTransform
	const float WarpSign = bEnableWarping && NewInteractingRolesNum > 1 ? 1.f : -1.f;
	if (InitialTranslationWarpTime > UE_KINDA_SMALL_NUMBER)
	{
		TranslationWarpLerp = FMath::Clamp(TranslationWarpLerp + (WarpSign * DeltaTime / InitialTranslationWarpTime), 0.f, 1.f);
	}
	else
	{
		TranslationWarpLerp = 0.f;
	}

	if (InitialRotationWarpTime > UE_KINDA_SMALL_NUMBER)
	{
		RotationWarpLerp = FMath::Clamp(RotationWarpLerp + (WarpSign * DeltaTime / InitialRotationWarpTime), 0.f, 1.f);
	}
	else
	{
		RotationWarpLerp = 0.f;
	}

	// bypassing FAnimNode_BlendStack::UpdateAssetPlayer, since we overridden its behaviour
	FAnimNode_BlendStack_Standalone::UpdateAssetPlayer(Context);

#if ENABLE_ANIM_DEBUG
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel))
	{
		TRACE_ANIM_NODE_VALUE(Context, *FString("InteractingRolesNum"), InteractingRolesNum);
		TRACE_ANIM_NODE_VALUE(Context, *FString("NewInteractingRolesNum"), NewInteractingRolesNum);
		TRACE_ANIM_NODE_VALUE(Context, *FString("BlendToExecuted"), bBlendToExecuted);
		TRACE_ANIM_NODE_VALUE(Context, *FString("TranslationWarpLerp"), TranslationWarpLerp);
		TRACE_ANIM_NODE_VALUE(Context, *FString("RotationWarpLerp"), RotationWarpLerp);
	}
#endif // ENABLE_ANIM_DEBUG

	InteractingRolesNum = NewInteractingRolesNum;
}

void FAnimNode_MotionMatchingInteraction::Evaluate_AnyThread(FPoseContext& Output)
{
	check(Output.AnimInstanceProxy);

	Super::Evaluate_AnyThread(Output);

	if (TranslationWarpLerp > UE_KINDA_SMALL_NUMBER || RotationWarpLerp > UE_KINDA_SMALL_NUMBER)
	{
		if (bUseAnimRootMotionProvider)
		{
			if (const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get())
			{
				RootMotionProvider->OverrideRootMotion(WarpRootMotionTransform, Output.CustomAttributes);
			}
			else
			{
				UE_LOG(LogPoseSearch, Error, TEXT("FAnimNode_MotionMatchingInteraction::Evaluate_AnyThread couldn't find the IAnimRootMotionProvider"));
			}
		}
		else
		{
			const FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();
			const FTransform FullAlignedActorRootBoneLocalTransform = FullAlignedActorRootBoneTransform.GetRelativeTransform(ComponentTransform);

#if ENABLE_ANIM_DEBUG
			if (CVarAnimNodeMotionMatchingInteractionDebug.GetValueOnAnyThread())
			{
				Output.AnimInstanceProxy->AnimDrawDebugCoordinateSystem(FullAlignedActorRootBoneTransform.GetLocation(), FullAlignedActorRootBoneTransform.Rotator(), 15.f, false, 0.f, 0.f, SDPG_Foreground);
				Output.AnimInstanceProxy->AnimDrawDebugCoordinateSystem(ComponentTransform.GetLocation(), ComponentTransform.Rotator(), 5.f, false, 0.f, 0.f, SDPG_Foreground);
			}
#endif // ENABLE_ANIM_DEBUG

			const FCompactPoseBoneIndex RootBoneIndex(0);
			FTransform& RootBoneTransform = Output.Pose[RootBoneIndex];
			RootBoneTransform.SetTranslation(FMath::Lerp(RootBoneTransform.GetTranslation(), FullAlignedActorRootBoneLocalTransform.GetTranslation(), TranslationWarpLerp));
			RootBoneTransform.SetRotation(FQuat::Slerp(RootBoneTransform.GetRotation(), FullAlignedActorRootBoneLocalTransform.GetRotation(), RotationWarpLerp));
		}
	}
}
