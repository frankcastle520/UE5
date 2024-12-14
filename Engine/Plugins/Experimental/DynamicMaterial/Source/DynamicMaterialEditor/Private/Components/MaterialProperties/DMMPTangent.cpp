// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPTangent.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertyTangent::UDMMaterialPropertyTangent()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::Tangent),
		EDMValueType::VT_Float3_XYZ)
{
}

UMaterialExpression* UDMMaterialPropertyTangent::GetDefaultInput(const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, FVector::OneVector);
}
