// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPBaseColor.h"
#include "Components/DMMaterialSlot.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertyBaseColor::UDMMaterialPropertyBaseColor()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::BaseColor),
		EDMValueType::VT_Float3_RGB)
{
}

UMaterialExpression* UDMMaterialPropertyBaseColor::GetDefaultInput(const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, FVector::OneVector);
}

TEnumAsByte<EMaterialSamplerType> UDMMaterialPropertyBaseColor::GetTextureSamplerType() const
{
	return EMaterialSamplerType::SAMPLERTYPE_Color;
}
