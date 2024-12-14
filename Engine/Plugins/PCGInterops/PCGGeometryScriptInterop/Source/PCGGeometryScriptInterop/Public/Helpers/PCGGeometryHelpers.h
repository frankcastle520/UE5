// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/PCGMeshSampler.h"

#include "ConversionUtils/SceneComponentToDynamicMesh.h"
#include "GeometryScript/GeometryScriptTypes.h"

struct FPCGContext;
class UGeometryScriptDebug;
class UMaterialInterface;

namespace UE::Geometry
{
	class FDynamicMesh3;
	struct FMeshIndexMappings;
}

namespace PCGGeometryHelpers
{
	PCGGEOMETRYSCRIPTINTEROP_API void GeometryScriptDebugToPCGLog(FPCGContext* Context, const UGeometryScriptDebug* Debug);
	UE::Conversion::EMeshLODType SafeConversionLODType(const EGeometryScriptLODType LODType);

	/**
	 * Adaptation of UGeometryScriptLibrary_MeshMaterialFunctions::RemapToNewMaterialIDsByMaterial, to work on UE::Geometry::FDynamicMesh3 and with optional mappings.
	 * @param InMesh: Mesh to modify
	 * @param FromMaterials: Original array of materials for the mesh
	 * @param ToMaterials: New array of materials for the mesh. Mutable. If the original material is not in the new array, add it.
	 * @param OptionalMappings: Optional mappings if we want to update a subset of the triangles in the mesh.
	 */
	void RemapMaterials(UE::Geometry::FDynamicMesh3& InMesh, const TArray<UMaterialInterface*>& FromMaterials, TArray<TObjectPtr<UMaterialInterface>>& ToMaterials, const UE::Geometry::FMeshIndexMappings* OptionalMappings = nullptr);
}
