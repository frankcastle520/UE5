// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMMaterialStageBlend.h"
#include "DMMSBContrastBase.generated.h"

class UMaterialExpression;
struct FDMMaterialBuildState;

UCLASS(MinimalAPI, BlueprintType, Abstract, ClassGroup = "Material Designer")
class UDMMaterialStageBlendContrastBase : public UDMMaterialStageBlend
{
	GENERATED_BODY()

public:
	DYNAMICMATERIALEDITOR_API UDMMaterialStageBlendContrastBase(const FText& InName);

private:
	UDMMaterialStageBlendContrastBase();
};
