// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMMaterialStageGradient.h"
#include "DMMSGRadial.generated.h"

struct FDMMaterialBuildState;
class UMaterialExpression;

UCLASS(MinimalAPI, BlueprintType, ClassGroup = "Material Designer")
class UDMMaterialStageGradientRadial : public UDMMaterialStageGradient
{
	GENERATED_BODY()

	UDMMaterialStageGradientRadial();

public:
	static TSoftObjectPtr<UMaterialFunctionInterface> RadialGradientFunction;
};
