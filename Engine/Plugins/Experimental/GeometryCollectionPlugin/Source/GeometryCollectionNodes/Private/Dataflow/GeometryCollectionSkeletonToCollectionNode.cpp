// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionSkeletonToCollectionNode.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"

void FSkeletonToCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType OutCollection;
		TObjectPtr<const USkeleton> SkeletonValue = GetValue<TObjectPtr<const USkeleton>>(Context, &Skeleton);
		if (SkeletonValue)
		{
			FGeometryCollectionEngineConversion::AppendSkeleton(SkeletonValue.Get(), FTransform::Identity, &OutCollection);
		}
		SetValue(Context, MoveTemp(OutCollection), &Collection);
	}
}


