// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "DMEDefs.h"

#include "DMMaterialModelFunctionLibrary.generated.h"

class FString;
class UDynamicMaterialModelBase;
class UDynamicMaterialModelDynamic;
class UMaterial;

/**
 * Material / Model Function Library
 */
UCLASS()
class UDMMaterialModelFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	DYNAMICMATERIALEDITOR_API static UDynamicMaterialInstance* ExportMaterial(UDynamicMaterialModelBase* InMaterialModelBase);

	DYNAMICMATERIALEDITOR_API static UDynamicMaterialInstance* ExportMaterial(UDynamicMaterialModelBase* InMaterialModel, const FString& InSavePath);

	DYNAMICMATERIALEDITOR_API static UMaterial* ExportGeneratedMaterial(UDynamicMaterialModelBase* InMaterialModelBase);

	DYNAMICMATERIALEDITOR_API static UMaterial* ExportGeneratedMaterial(UDynamicMaterialModelBase* InMaterialModelBase, const FString& InSavePath);

	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModel* ExportToTemplateMaterialModel(UDynamicMaterialModelDynamic* InMaterialModelDynamic);

	DYNAMICMATERIALEDITOR_API static UDynamicMaterialModel* ExportToTemplateMaterialModel(UDynamicMaterialModelDynamic* InMaterialModelDynamic, const FString& InSavePath);

	DYNAMICMATERIALEDITOR_API static UDynamicMaterialInstance* ExportToTemplateMaterial(UDynamicMaterialModelDynamic* InMaterialModelDynamic);

	DYNAMICMATERIALEDITOR_API static UDynamicMaterialInstance* ExportToTemplateMaterial(UDynamicMaterialModelDynamic* InMaterialModelDynamic, const FString& InSavePath);

	DYNAMICMATERIALEDITOR_API static bool IsModelValid(UDynamicMaterialModelBase* InMaterialModelBase);

	DYNAMICMATERIALEDITOR_API static bool DuplicateModelBetweenMaterials(UDynamicMaterialModel* InFromModel, UDynamicMaterialInstance* InToInstance);

	DYNAMICMATERIALEDITOR_API static bool CreateModelInstanceInMaterial(UDynamicMaterialModel* InFromModel, UDynamicMaterialInstance* InToInstance);

	DYNAMICMATERIALEDITOR_API static FString RemoveAssetPrefix(const FString& InAssetName);
};
