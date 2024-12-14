// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_MotionMatchingInteraction.h"

#define LOCTEXT_NAMESPACE "MotionMatchingInteraction"

UAnimGraphNode_MotionMatchingInteraction::UAnimGraphNode_MotionMatchingInteraction(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

FText UAnimGraphNode_MotionMatchingInteraction::GetTooltipText() const
{
	return LOCTEXT("MotionMatchingInteractionTooltip", "Motion Matching Interaction Node");
}

FText UAnimGraphNode_MotionMatchingInteraction::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("MotionMatchingInteraction", "MotionMatchingInteraction");
}

FLinearColor UAnimGraphNode_MotionMatchingInteraction::GetNodeTitleColor() const
{
	return FLinearColor(FColor(6, 9, 53));
}

#undef LOCTEXT_NAMESPACE
