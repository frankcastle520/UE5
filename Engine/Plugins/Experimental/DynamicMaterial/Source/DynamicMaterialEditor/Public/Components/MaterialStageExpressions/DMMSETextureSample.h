// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/MaterialStageExpressions/DMMSETextureSampleBase.h"

#include "DMMSETextureSample.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = "Material Designer")
class UDMMaterialStageExpressionTextureSample : public UDMMaterialStageExpressionTextureSampleBase
{
	GENERATED_BODY()

	friend class SDMMaterialComponentEditor;

public:
	UDMMaterialStageExpressionTextureSample();
};
