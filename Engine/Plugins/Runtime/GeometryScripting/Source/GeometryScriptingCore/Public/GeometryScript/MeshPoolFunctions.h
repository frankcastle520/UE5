// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshPoolFunctions.generated.h"

class UDynamicMeshPool;


UCLASS(meta = (ScriptName = "GeometryScript_MeshPoolUtility"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshPoolFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/** Access a global compute mesh pool (created on first access) */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshPool")
	static UPARAM(DisplayName = "Mesh Pool") UDynamicMeshPool* 
	GetGlobalMeshPool();

	/** Fully clear/destroy the current global mesh pool, allowing it and all its meshes to be garbage collected */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshPool")
	static void DiscardGlobalMeshPool();
};