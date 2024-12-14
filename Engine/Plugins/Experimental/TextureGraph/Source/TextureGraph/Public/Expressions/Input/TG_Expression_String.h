// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TG_Expression_InputParam.h"
#include "TG_Expression_String.generated.h"

UCLASS()
class TEXTUREGRAPH_API UTG_Expression_String : public UTG_Expression_InputParam
{
	GENERATED_BODY()
	TG_DECLARE_INPUT_PARAM_EXPRESSION(TG_Category::Input);

public:

	virtual void Evaluate(FTG_EvaluationContext* InContext) override;

	// The string constant
	UPROPERTY(EditAnywhere, Category = NoCategory, meta = (TGType = "TG_InputParam", PinDisplayName = ""))
		FString String;

	// The output of the node, which is the string constant
	UPROPERTY(meta = (TGType = "TG_Output", PinDisplayName = ""))
		FString ValueOut;

	virtual FTG_Name GetDefaultName() const override { return TEXT("String"); }
	virtual void SetTitleName(FName NewName) override;
	virtual FName GetTitleName() const override;
	virtual FText GetTooltipText() const override { return FText::FromString(TEXT("String parameter that can be used as input in various places.")); } 
};

