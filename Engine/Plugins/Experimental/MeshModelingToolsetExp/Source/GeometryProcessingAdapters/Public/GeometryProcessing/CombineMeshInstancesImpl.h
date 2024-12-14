// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryProcessingInterfaces/CombineMeshInstances.h"

class UMaterialInterface;

namespace UE
{
namespace Geometry
{

class FDynamicMesh3;

/**
 * Implementation of IGeometryProcessing_CombineMeshInstances
 */
class GEOMETRYPROCESSINGADAPTERS_API FCombineMeshInstancesImpl : public IGeometryProcessing_CombineMeshInstances
{
public:
	virtual FOptions ConstructDefaultOptions() override;

	virtual void CombineMeshInstances(const FSourceInstanceList& MeshInstances, const FOptions& Options, FResults& ResultsOut) override;

	virtual void CombineMeshInstances(
		const FSourceInstanceList& MeshInstances,
		const FCombineMeshInstancesOptionsGeneral& AllLODOptions,
		TConstArrayView<FCombineMeshInstancesOptionsPerLOD> PerLODOptions,
		FResults& ResultsOut
	) override;


	virtual void ComputeSinglePartMeshSet(
		TConstArrayView<const FMeshDescription*> SourceMeshLODs,
		const FComputePartMeshesOptions& GeneralOptions,
		const FComputePartMeshesSinglePartOptions& PartOptions,
		FSinglePartMeshSet& ResultMeshes
	) override;

	virtual void ComputeSinglePartMeshSet(
		UStaticMesh* SourceMesh,
		const FComputePartMeshesOptions& GeneralOptions,
		const FComputePartMeshesSinglePartOptions& PartOptions,
		FSinglePartMeshSet& ResultPartMeshSet
	) override;

	virtual void ComputePartMeshSets(
		FSourceInstanceList& MeshInstances,
		const FComputePartMeshesOptions& GeneralOptions,
		bool bKeepExistingPartMeshes,
		TArray<TSharedPtr<FSinglePartMeshSet>>& ResultMeshSets
	) override;


protected:

};


}
}
