// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "K2Node_BaseAsyncTask.h"

#include "K2Node_AnimNextPlayAsset.generated.h"

UCLASS()
class UK2Node_AnimNextPlayAsset : public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

	UK2Node_AnimNextPlayAsset();

	// UEdGraphNode Interface
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	// UK2Node Interface
	virtual FText GetMenuCategory() const override;
};
