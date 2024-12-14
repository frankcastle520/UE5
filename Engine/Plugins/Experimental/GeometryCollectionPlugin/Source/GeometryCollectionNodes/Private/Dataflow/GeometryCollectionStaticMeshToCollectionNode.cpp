// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionStaticMeshToCollectionNode.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"


// ===========================================================================================================================


FStaticMeshToCollectionDataflowNode::FStaticMeshToCollectionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&StaticMesh);
	RegisterInputConnection(&MeshTransform);
	RegisterOutputConnection(&Collection);
	RegisterOutputConnection(&Materials);
	RegisterOutputConnection(&MaterialInstances);
	RegisterOutputConnection(&InstancedMeshes);
}

void FStaticMeshToCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	ensure(Out->IsA(&Collection) || Out->IsA(&Materials) || Out->IsA(&InstancedMeshes));

	FManagedArrayCollection OutCollection;
	TArray<TObjectPtr<UMaterialInterface>> OutMaterialInstances;
	TArray<FGeometryCollectionAutoInstanceMesh> OutInstancedMeshes;

	TObjectPtr<UStaticMesh> StaticMeshVal = GetValue(Context, &StaticMesh, StaticMesh);
	const FTransform& InMeshTransform = GetValue(Context, &MeshTransform, MeshTransform);
	if (StaticMeshVal)
	{
		FGeometryCollectionEngineConversion::ConvertStaticMeshToGeometryCollection(StaticMeshVal, InMeshTransform, OutCollection, OutMaterialInstances, OutInstancedMeshes, bSetInternalFromMaterialIndex, bSplitComponents);
	}

	TArray<TObjectPtr<UMaterial>> OutMaterials;
	FGeometryCollectionEngineConversion::GetMaterialsFromInstances(OutMaterialInstances, OutMaterials);

	// Set Outputs
	SetValue(Context, MoveTemp(OutCollection), &Collection);
	SetValue(Context, MoveTemp(OutMaterials), &Materials);
	SetValue(Context, MoveTemp(OutMaterialInstances), &MaterialInstances);
	SetValue(Context, MoveTemp(OutInstancedMeshes), &InstancedMeshes);
}

