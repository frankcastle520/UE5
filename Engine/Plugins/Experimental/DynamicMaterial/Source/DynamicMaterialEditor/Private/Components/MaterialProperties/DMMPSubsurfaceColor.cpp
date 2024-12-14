// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPSubsurfaceColor.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertySubsurfaceColor::UDMMaterialPropertySubsurfaceColor()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::SubsurfaceColor),
		EDMValueType::VT_Float3_RGB)
{
}

UMaterialExpression* UDMMaterialPropertySubsurfaceColor::GetDefaultInput(
	const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, FVector::OneVector);
}

TEnumAsByte<EMaterialSamplerType> UDMMaterialPropertySubsurfaceColor::GetTextureSamplerType() const
{
	return EMaterialSamplerType::SAMPLERTYPE_Color;
}
