// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMMaterialProperty.h"
#include "DMMPEmissiveColor.generated.h"

class UDynamicMaterialModelEditorOnlyData;

UCLASS(BlueprintType, ClassGroup = "Material Designer")
class UDMMaterialPropertyEmissiveColor : public UDMMaterialProperty
{
	GENERATED_BODY()

public:
	UDMMaterialPropertyEmissiveColor();

	//~ Begin UDMMaterialProperty
	virtual UMaterialExpression* GetDefaultInput(const TSharedRef<FDMMaterialBuildState>& InBuildState) const override;
	virtual TEnumAsByte<EMaterialSamplerType> GetTextureSamplerType() const override;
	//~ End UDMMaterialProperty
};
