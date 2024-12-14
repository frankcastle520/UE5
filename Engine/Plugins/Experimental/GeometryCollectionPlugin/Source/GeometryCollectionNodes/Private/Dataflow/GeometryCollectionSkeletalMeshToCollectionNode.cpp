// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionSkeletalMeshToCollectionNode.h"

#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "MeshDescription.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "IndexTypes.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

void FSkeletalMeshToCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		FGeometryCollection OutCollection;
		TObjectPtr<const USkeletalMesh> InSkeletalMesh = GetValue<TObjectPtr<const USkeletalMesh>>(Context, &SkeletalMesh);
		if (InSkeletalMesh)
		{
			FGeometryCollectionEngineConversion::AppendSkeletalMesh(InSkeletalMesh, 0, FTransform::Identity, &OutCollection, /*bReindexMaterials = */ true, bImportTransformOnly);
		}
		SetValue(Context, FManagedArrayCollection(OutCollection), &Collection);
	}
}

