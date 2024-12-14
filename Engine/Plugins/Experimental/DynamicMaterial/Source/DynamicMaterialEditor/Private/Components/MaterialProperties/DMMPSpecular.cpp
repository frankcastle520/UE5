// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MaterialProperties/DMMPSpecular.h"
#include "Model/DynamicMaterialModelEditorOnlyData.h"

UDMMaterialPropertySpecular::UDMMaterialPropertySpecular()
	: UDMMaterialProperty(
		EDMMaterialPropertyType(EDMMaterialPropertyType::Specular),
		EDMValueType::VT_Float3_RGB)
{
}

UMaterialExpression* UDMMaterialPropertySpecular::GetDefaultInput(const TSharedRef<FDMMaterialBuildState>& InBuildState) const
{
	return CreateConstant(InBuildState, FVector::OneVector);
}
