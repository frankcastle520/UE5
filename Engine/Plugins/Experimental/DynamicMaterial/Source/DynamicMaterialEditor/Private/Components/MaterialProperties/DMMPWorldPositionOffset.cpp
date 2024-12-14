// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPWorldPositionOffset.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertyWorldPositionOffset::UDMMaterialPropertyWorldPositionOffset()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::WorldPositionOffset),
		EDMValueType::VT_Float3_XYZ)
{
}

UMaterialExpression* UDMMaterialPropertyWorldPositionOffset::GetDefaultInput(
	const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, FVector::ZeroVector);
}
