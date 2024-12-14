// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimGraphNode_Base.h"
#include "BoneControllers/AnimNode_OverrideRootMotion.h"

#include "AnimGraphNode_OverrideRootMotion.generated.h"

namespace ENodeTitleType { enum Type : int; }

UCLASS(Experimental)
class ANIMATIONWARPINGEDITOR_API UAnimGraphNode_OverrideRootMotion : public UAnimGraphNode_Base
{
	GENERATED_UCLASS_BODY()


	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_OverrideRootMotion Node;

public:
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual void GetInputLinkAttributes(FNodeAttributeArray& OutAttributes) const override;
	virtual void GetOutputLinkAttributes(FNodeAttributeArray& OutAttributes) const override;
	// End of UEdGraphNode interface

};
