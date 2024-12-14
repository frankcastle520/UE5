// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimGraphNode_BlendStack.h"
#include "PoseSearch/AnimNode_MotionMatchingInteraction.h"
#include "AnimGraphNode_MotionMatchingInteraction.generated.h"

UCLASS(Experimental)
class UAnimGraphNode_MotionMatchingInteraction : public UAnimGraphNode_BlendStack_Base
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_MotionMatchingInteraction Node;

public:
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;

protected:
	virtual FAnimNode_BlendStack_Standalone* GetBlendStackNode() const override { return (FAnimNode_BlendStack_Standalone*)(&Node); }
};
