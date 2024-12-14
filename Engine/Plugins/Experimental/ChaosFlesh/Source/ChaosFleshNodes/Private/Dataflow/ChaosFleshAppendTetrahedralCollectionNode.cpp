// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshAppendTetrahedralCollectionNode.h"

#include "ChaosFlesh/FleshCollection.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "GeometryCollection/Facades/CollectionTransformFacade.h"

void FAppendTetrahedralCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection1))
	{
		TUniquePtr<FFleshCollection> InCollection1(GetValue<FManagedArrayCollection>(Context, &Collection1).NewCopy<FFleshCollection>());
		TUniquePtr<FFleshCollection> InCollection2(GetValue<FManagedArrayCollection>(Context, &Collection2).NewCopy<FFleshCollection>());
		TArray<FString> GeometryGroupGuidsLocal1, GeometryGroupGuidsLocal2;
		if (const TManagedArray<FString>* GuidArray1 = InCollection1->FindAttribute<FString>("Guid", FGeometryCollection::GeometryGroup))
		{
			GeometryGroupGuidsLocal1 = GuidArray1->GetConstArray();
		}
		GeometryCollection::Facades::FCollectionTransformFacade InTransformFacade1(*InCollection1);
		GeometryCollection::Facades::FCollectionTransformFacade InTransformFacade2(*InCollection2);
		const TMap<FString, int32> InBoneNameMap = InTransformFacade1.BoneNameIndexMap();
		const int32 InNumTransform2 = InTransformFacade2.Num();
		const int32 InNumTransform1 = InTransformFacade1.Num();
		//Append
		InCollection1->AppendCollection(*InCollection2);

		if (const TManagedArray<FString>* GuidArray2 = InCollection2->FindAttribute<FString>("Guid", FGeometryCollection::GeometryGroup))
		{
			GeometryGroupGuidsLocal2 = GuidArray2->GetConstArray();
		}
		if (bMergeTransform)
		{
			//Reorder and delete transform
			const TManagedArray<FString>* OutBoneNames = InTransformFacade1.FindBoneNames();
			TArray<int32> MergeRemapIndex;
			TArray<int32> SortedMergeList;
			for (int32 Idx = 0; Idx < InNumTransform2; ++Idx)
			{
				const FString& BoneName = (*OutBoneNames)[Idx];
				if (Idx < InNumTransform2 && InBoneNameMap.Contains(BoneName))
				{
					SortedMergeList.Add(Idx);
					MergeRemapIndex.Add(InBoneNameMap[BoneName] + InNumTransform2); //SKM collection is appended to the front
				}
			}
			InCollection1->MergeElements(FTransformCollection::TransformGroup, SortedMergeList, MergeRemapIndex);
		}
		const int32 NumGeometries = InCollection1->NumElements(FGeometryCollection::GeometryGroup);
		const int32 NumGeometries2 = InCollection2->NumElements(FGeometryCollection::GeometryGroup);
		FDataflowGeometrySelection InGeometrySelection1;
		FDataflowGeometrySelection InGeometrySelection2;
		InGeometrySelection1.Initialize(NumGeometries, false);
		InGeometrySelection2.Initialize(NumGeometries, false);
		for (int32 GeometryIdx = 0; GeometryIdx < NumGeometries2; ++GeometryIdx)
		{
			InGeometrySelection2.SetSelected(GeometryIdx);
		}
		for (int32 GeometryIdx = NumGeometries2; GeometryIdx < NumGeometries; ++GeometryIdx)
		{
			InGeometrySelection1.SetSelected(GeometryIdx);
		}
		SetValue<const FManagedArrayCollection&>(Context, MoveTemp(*InCollection1), &Collection1);
		SetValue<FDataflowGeometrySelection>(Context, MoveTemp(InGeometrySelection1), &GeometrySelection1);
		SetValue<FDataflowGeometrySelection>(Context, MoveTemp(InGeometrySelection2), &GeometrySelection2);
		SetValue(Context, MoveTemp(GeometryGroupGuidsLocal1), &GeometryGroupGuidsOut1);
		SetValue(Context, MoveTemp(GeometryGroupGuidsLocal2), &GeometryGroupGuidsOut2);
	}
}