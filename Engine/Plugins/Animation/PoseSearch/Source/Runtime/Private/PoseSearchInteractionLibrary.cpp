// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "Animation/AnimMontage.h"
#include "PoseSearch/PoseSearchInteractionSubsystem.h"
#include "Engine/World.h"

FPoseSearchInteractionBlueprintResult UPoseSearchInteractionLibrary::MotionMatchInteraction_Pure(TArray<FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, FPoseSearchContinuingProperties ContinuingProperties, FName PoseHistoryName, bool bValidateResultAgainstAvailabilities)
{
	FPoseSearchInteractionBlueprintResult Result;
	if (UPoseSearchInteractionSubsystem* InteractionSubsystem = UPoseSearchInteractionSubsystem::GetSubsystem_AnyThread(AnimInstance))
	{
		InteractionSubsystem->Query_AnyThread(Availabilities, AnimInstance, ContinuingProperties, Result, PoseHistoryName, nullptr, bValidateResultAgainstAvailabilities);
	}
	return Result;
}

FPoseSearchInteractionBlueprintResult UPoseSearchInteractionLibrary::MotionMatchInteraction(TArray<FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, FPoseSearchContinuingProperties ContinuingProperties, FName PoseHistoryName, bool bValidateResultAgainstAvailabilities)
{
	return MotionMatchInteraction_Pure(Availabilities, AnimInstance, ContinuingProperties, PoseHistoryName, bValidateResultAgainstAvailabilities);
}

FPoseSearchInteractionBlueprintResult UPoseSearchInteractionLibrary::MotionMatchInteraction(const TArrayView<const FPoseSearchInteractionAvailability> Availabilities, UObject* AnimInstance, const FPoseSearchContinuingProperties& ContinuingProperties, const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector, bool bValidateResultAgainstAvailabilities)
{
	FPoseSearchInteractionBlueprintResult Result;
	if (UPoseSearchInteractionSubsystem* InteractionSubsystem = UPoseSearchInteractionSubsystem::GetSubsystem_AnyThread(AnimInstance))
	{
		InteractionSubsystem->Query_AnyThread(Availabilities, AnimInstance, ContinuingProperties, Result, FName(), HistoryCollector, bValidateResultAgainstAvailabilities);
	}
	return Result;
}

FPoseSearchContinuingProperties UPoseSearchInteractionLibrary::GetMontageContinuingProperties(UAnimInstance* AnimInstance)
{
	FPoseSearchContinuingProperties ContinuingProperties;
	if (const FAnimMontageInstance* AnimMontageInstance = AnimInstance->GetActiveMontageInstance())
	{
		ContinuingProperties.PlayingAsset = AnimMontageInstance->Montage;
		ContinuingProperties.PlayingAssetAccumulatedTime = AnimMontageInstance->DeltaTimeRecord.GetPrevious();
	}
	return ContinuingProperties;
}