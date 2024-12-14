// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "GeometryCollectionBlueprintLibrary.generated.h"

/** Blueprint library for Geometry Collections. */
UCLASS(meta = (ScriptName = "GeometryCollectionLibrary"))
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollectionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 
	 * Set a custom instance data value for all instances associated with a geometry collection. 
	 * This assumes that the geometry collection is using a custom renderer that supports IGeometryCollectionCustomDataInterface.
	 * @param GeometryCollectionComponent	The Geometry Collection Component that we want to set custom instance data on.
	 * @param CustomDataIndex	The index of the custom instance data slot that we want to set.
	 * @param CustomDataValue	The value to set to the custom instance data slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "ChaosPhysics")
	static void SetCustomInstanceDataByIndex(UGeometryCollectionComponent* GeometryCollectionComponent, int32 CustomDataIndex, float CustomDataValue);

	/** 
	 * Set a custom instance data value for all instances associated with a geometry collection. 
	 * This assumes that the geometry collection is using a custom renderer that supports IGeometryCollectionCustomDataInterface.
	 * @param GeometryCollectionComponent	The Geometry Collection Component that we want to set custom instance data on.
	 * @param CustomDataName	The name of the custom instance data slot that we want to set.
	 * @param CustomDataValue	The value to set to the custom instance data slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "ChaosPhysics")
	static void SetCustomInstanceDataByName(UGeometryCollectionComponent* GeometryCollectionComponent, FName CustomDataName, float CustomDataValue);

	UE_DEPRECATED(5.5, "Use SetCustomInstanceDataByIndex() instead")
	UFUNCTION(BlueprintCallable, Category = "ChaosPhysics", meta = (DeprecatedFunction, DeprecationMessage = "Use SetCustomInstanceDataByIndex() instead"))
	static void SetISMPoolCustomInstanceData(UGeometryCollectionComponent* GeometryCollectionComponent, int32 CustomDataIndex, float CustomDataValue);
};
