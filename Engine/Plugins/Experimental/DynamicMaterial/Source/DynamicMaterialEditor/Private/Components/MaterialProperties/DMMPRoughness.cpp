// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPRoughness.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertyRoughness::UDMMaterialPropertyRoughness()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::Roughness),
		EDMValueType::VT_Float1)
{
}

UMaterialExpression* UDMMaterialPropertyRoughness::GetDefaultInput(const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, 0.f);
}
