// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPOpacityMask.h"

#include "Components/MaterialProperties/DMMPOpacity.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertyOpacityMask::UDMMaterialPropertyOpacityMask()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::OpacityMask),
		EDMValueType::VT_Float1)
{
}

UMaterialExpression* UDMMaterialPropertyOpacityMask::GetDefaultInput(
	const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, 1.f);
}

TEnumAsByte<EMaterialSamplerType> UDMMaterialPropertyOpacityMask::GetTextureSamplerType() const
{
	return EMaterialSamplerType::SAMPLERTYPE_Masks;
}
