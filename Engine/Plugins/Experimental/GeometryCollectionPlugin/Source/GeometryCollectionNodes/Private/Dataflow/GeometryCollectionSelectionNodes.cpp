// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionSelectionNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "Logging/LogMacros.h"
#include "Templates/SharedPointer.h"
#include "UObject/UnrealTypePrivate.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshEditor.h"
#include "Operations/MeshBoolean.h"

#include "EngineGlobals.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "FractureEngineClustering.h"
#include "FractureEngineSelection.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionSelectionNodes)

namespace UE::Dataflow
{

	void GeometryCollectionSelectionNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionAllDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionSetOperationDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionInfoDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionNoneDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionInvertDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionRandomDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionRootDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionCustomDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionFromIndexArrayDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionParentDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionByPercentageDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionChildrenDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionSiblingsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionLevelDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionTargetLevelDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionContactDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionLeafDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionClusterDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionClusterDataflowNode_v2);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionBySizeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionByVolumeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionInBoxDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionInSphereDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionByFloatAttrDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSelectFloatArrayIndicesInRangeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionTransformSelectionByIntAttrDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionVertexSelectionCustomDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionFaceSelectionCustomDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionSelectionConvertDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionFaceSelectionInvertDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionVertexSelectionByPercentageDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionVertexSelectionSetOperationDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCollectionSelectionByAttrDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGeometrySelectionToVertexSelectionDataflowNode);
		// GeometryCollection|Selection
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("GeometryCollection|Selection", FLinearColor(1.f, 1.f, 0.05f), CDefaultNodeBodyTintColor);
	}
}


void FCollectionTransformSelectionAllDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectAll();

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionSetOperationDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FDataflowTransformSelection& InTransformSelectionA = GetValue<FDataflowTransformSelection>(Context, &TransformSelectionA);
		const FDataflowTransformSelection& InTransformSelectionB = GetValue<FDataflowTransformSelection>(Context, &TransformSelectionB);

		FDataflowTransformSelection NewTransformSelection;

		if (InTransformSelectionA.Num() == InTransformSelectionB.Num())
		{
			if (Operation == ESetOperationEnum::Dataflow_SetOperation_AND)
			{
				InTransformSelectionA.AND(InTransformSelectionB, NewTransformSelection);
			}
			else if (Operation == ESetOperationEnum::Dataflow_SetOperation_OR)
			{
				InTransformSelectionA.OR(InTransformSelectionB, NewTransformSelection);
			}
			else if (Operation == ESetOperationEnum::Dataflow_SetOperation_XOR)
			{
				InTransformSelectionA.XOR(InTransformSelectionB, NewTransformSelection);
			}
			else if (Operation == ESetOperationEnum::Dataflow_SetOperation_Subtract)
			{
				InTransformSelectionA.Subtract(InTransformSelectionB, NewTransformSelection);
			}
		}
		else
		{
			// ERROR: INPUT TRANSFORMSELECTIONS HAVE DIFFERENT NUMBER OF ELEMENTS
			FString ErrorStr = "Input TransformSelections have different number of elements.";
			UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] %s"), *ErrorStr);
		}

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
}


namespace {
	struct BoneInfo {
		int32 BoneIndex;
		int32 Level;
	};
}

static void ExpandRecursive(const int32 BoneIndex, int32 Level, const TManagedArray<TSet<int32>>& Children, TArray<BoneInfo>& BoneHierarchy)
{
	BoneHierarchy.Add({ BoneIndex, Level });

	TSet<int32> ChildrenSet = Children[BoneIndex];
	if (ChildrenSet.Num() > 0)
	{
		for (auto& Child : ChildrenSet)
		{
			ExpandRecursive(Child, Level + 1, Children, BoneHierarchy);
		}
	}
}

static void BuildHierarchicalOutput(const TManagedArray<int32>& Parents, 
	const TManagedArray<TSet<int32>>& Children, 
	const TManagedArray<FString>& BoneNames,
	const FDataflowTransformSelection& TransformSelection, 
	FString& OutputStr)
{
	TArray<BoneInfo> BoneHierarchy;

	int32 NumElements = Parents.Num();
	for (int32 Index = 0; Index < NumElements; ++Index)
	{
		if (Parents[Index] == FGeometryCollection::Invalid)
		{
			ExpandRecursive(Index, 0, Children, BoneHierarchy);
		}
	}

	// Get level max
	int32 LevelMax = -1;
	int32 BoneNameLengthMax = -1;
	for (int32 Idx = 0; Idx < BoneHierarchy.Num(); ++Idx)
	{
		if (BoneHierarchy[Idx].Level > LevelMax)
		{
			LevelMax = BoneHierarchy[Idx].Level;
		}

		int32 BoneNameLength = BoneNames[Idx].Len();
		if (BoneNameLength > BoneNameLengthMax)
		{
			BoneNameLengthMax = BoneNameLength;
		}
	}

	const int32 BoneIndexWidth = 2 + LevelMax * 2 + 6;
	const int32 BoneNameWidth = BoneNameLengthMax + 2;
	const int32 SelectedWidth = 10;

	for (int32 Idx = 0; Idx < BoneHierarchy.Num(); ++Idx)
	{
		FString BoneIndexStr, BoneNameStr;
		BoneIndexStr.Reserve(BoneIndexWidth);
		BoneNameStr.Reserve(BoneNameWidth);

		if (BoneHierarchy[Idx].Level == 0)
		{
			BoneIndexStr.Appendf(TEXT("[%d]"), BoneHierarchy[Idx].BoneIndex);
		}
		else
		{
			BoneIndexStr.Appendf(TEXT(" |"));
			for (int32 Idx1 = 0; Idx1 < BoneHierarchy[Idx].Level; ++Idx1)
			{
				BoneIndexStr.Appendf(TEXT("--"));
			}
			BoneIndexStr.Appendf(TEXT("[%d]"), BoneHierarchy[Idx].BoneIndex);
		}
		BoneIndexStr = BoneIndexStr.RightPad(BoneIndexWidth);

		BoneNameStr.Appendf(TEXT("%s"), *BoneNames[Idx]);
		BoneNameStr = BoneNameStr.RightPad(BoneNameWidth);

		OutputStr.Appendf(TEXT("%s%s%s\n\n"), *BoneIndexStr, *BoneNameStr, (TransformSelection.IsSelected(BoneHierarchy[Idx].BoneIndex) ? TEXT("Selected") : TEXT("---")));
	}

}


void FCollectionTransformSelectionInfoDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&String))
	{
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FString OutputStr;

		OutputStr.Appendf(TEXT("\n----------------------------------------\n"));
		OutputStr.Appendf(TEXT("Number of Elements: %d\n"), InTransformSelection.Num());

		// Hierarchical display
		if (InCollection.HasGroup(FGeometryCollection::TransformGroup) &&
			InCollection.HasAttribute("Parent", FGeometryCollection::TransformGroup) &&
			InCollection.HasAttribute("Children", FGeometryCollection::TransformGroup) &&
			InCollection.HasAttribute("BoneName", FGeometryCollection::TransformGroup))
		{
			if (InTransformSelection.Num() == InCollection.NumElements(FGeometryCollection::TransformGroup))
			{
				const TManagedArray<int32>& Parents = InCollection.GetAttribute<int32>("Parent", FGeometryCollection::TransformGroup);
				const TManagedArray<TSet<int32>>& Children = InCollection.GetAttribute<TSet<int32>>("Children", FGeometryCollection::TransformGroup);
				const TManagedArray<FString>& BoneNames = InCollection.GetAttribute<FString>("BoneName", FGeometryCollection::TransformGroup);

				BuildHierarchicalOutput(Parents, Children, BoneNames, InTransformSelection, OutputStr);
			}
			else
			{
				// ERROR: TransformSelection doesn't match the Collection
				FString ErrorStr = "TransformSelection doesn't match the Collection.";
				UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] %s"), *ErrorStr);
			}
		}
		else
		// Simple display
		{
			for (int32 Idx = 0; Idx < InTransformSelection.Num(); ++Idx)
			{
				OutputStr.Appendf(TEXT("%4d: %s\n"), Idx, (InTransformSelection.IsSelected(Idx) ? TEXT("Selected") : TEXT("---")));
			}
		}

		OutputStr.Appendf(TEXT("----------------------------------------\n"));

		SetValue(Context, MoveTemp(OutputStr), &String);
	}
}


void FCollectionTransformSelectionNoneDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectNone();

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionInvertDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		InTransformSelection.Invert();

		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
}


void FCollectionTransformSelectionRandomDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		float RandomSeedVal = GetValue<float>(Context, &RandomSeed);
		float RandomThresholdVal = GetValue<float>(Context, &RandomThreshold);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectRandom(bDeterministic, RandomSeedVal, RandomThresholdVal);

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionRootDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectRootBones();

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionCustomDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (InCollection.HasGroup(FGeometryCollection::TransformGroup))
		{
			const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(NumTransforms, false);

			const FString InBoneIndices = GetValue<FString>(Context, &BoneIndicies);

			TArray<FString> Indices;
			InBoneIndices.ParseIntoArray(Indices, TEXT(" "), true);

			for (FString IndexStr : Indices)
			{
				if (IndexStr.IsNumeric())
				{
					int32 Index = FCString::Atoi(*IndexStr);
					if (Index >= 0 && Index < NumTransforms)
					{
						NewTransformSelection.SetSelected(Index);
					}
					else
					{
						// ERROR: INVALID INDEX
						FString ErrorStr = "Invalid specified index found.";
						UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] %s"), *ErrorStr);
					}
				}
			}

			SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
		}
		else
		{
			SetValue(Context, FDataflowTransformSelection(), &TransformSelection);
		}
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionFromIndexArrayDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);

		if (InCollection.HasGroup(FGeometryCollection::TransformGroup))
		{
			const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);

			const TArray<int32>& InBoneIndices = GetValue(Context, &BoneIndices);

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(NumTransforms, false);
			for (int32 SelectedIdx : InBoneIndices)
			{
				if (SelectedIdx >= 0 && SelectedIdx < NumTransforms)
				{
					NewTransformSelection.SetSelected(SelectedIdx);
				}
				else
				{
					UE_LOG(LogChaos, Error, TEXT("[Dataflow ERROR] Invalid selection index %d is outside valid bone index range [0, %d)"), SelectedIdx, NumTransforms);
				}
			}

			SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
		}
		else
		{
			SetValue(Context, FDataflowTransformSelection(), &TransformSelection);
		}
	}
	else if (Out->IsA(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionParentDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		TArray<int32> SelectionArr = InTransformSelection.AsArray();
		TransformSelectionFacade.SelectParent(SelectionArr);

		InTransformSelection.SetFromArray(SelectionArr);
		
		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionByPercentageDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		int32 InPercentage = GetValue<int32>(Context, &Percentage);
		float InRandomSeed = GetValue<float>(Context, &RandomSeed);

		TArray<int32> SelectionArr = InTransformSelection.AsArray();

		GeometryCollection::Facades::FCollectionTransformSelectionFacade::SelectByPercentage(SelectionArr, InPercentage, bDeterministic, InRandomSeed);

		InTransformSelection.SetFromArray(SelectionArr);
		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
}


void FCollectionTransformSelectionChildrenDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		TArray<int32> SelectionArr = InTransformSelection.AsArray();

		TransformSelectionFacade.SelectChildren(SelectionArr);
		InTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionSiblingsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		TArray<int32> SelectionArr = InTransformSelection.AsArray();

		TransformSelectionFacade.SelectSiblings(SelectionArr);
		InTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionLevelDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		TArray<int32> SelectionArr = InTransformSelection.AsArray();

		TransformSelectionFacade.SelectLevel(SelectionArr);
		InTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionTargetLevelDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);

		int InTargetLevel = GetValue(Context, &TargetLevel);

		TArray<int32> AllAtLevel = TransformSelectionFacade.GetBonesExactlyAtLevel(InTargetLevel, bSkipEmbedded);

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(AllAtLevel);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionContactDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		TArray<int32> SelectionArr = InTransformSelection.AsArray();

		TransformSelectionFacade.SelectContact(SelectionArr, bAllowContactInParentLevels);
		InTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(InTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionLeafDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectLeaf();

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionClusterDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		// this node used to use SelectCluster() but this was buggy and woudl select the leaves instead
		// for this reason this node is now deprecated and we need to keep it doing what it sued to : SelectLeaf()
		// version 2 of the node properly use the right way 
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectLeaf(); // used to be buggy SelectCluster() - see comment above 

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FCollectionTransformSelectionClusterDataflowNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectCluster(); 

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FCollectionTransformSelectionBySizeDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		float InSizeMin = GetValue<float>(Context, &SizeMin);
		float InSizeMax = GetValue<float>(Context, &SizeMax);
		bool bInsideRange = RangeSetting == ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectBySize(InSizeMin, InSizeMax, bInclusive, bInsideRange, bUseRelativeSize);

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionByVolumeDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		float InVolumeMin = GetValue<float>(Context, &VolumeMin);
		float InVolumeMax = GetValue<float>(Context, &VolumeMax);
		bool bInsideRange = RangeSetting == ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectByVolume(InVolumeMin, InVolumeMax, bInclusive, bInsideRange);

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionInBoxDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FBox& InBox = GetValue<FBox>(Context, &Box);
		const FTransform& InTransform = GetValue<FTransform>(Context, &Transform);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);

		TArray<int32> SelectionArr;
		if (Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Vertices)
		{
			SelectionArr = TransformSelectionFacade.SelectVerticesInBox(InBox, InTransform, bAllVerticesMustContainedInBox);
		}
		else if (Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_BoundingBox)
		{
			SelectionArr = TransformSelectionFacade.SelectBoundingBoxInBox(InBox, InTransform);
		}
		else if (Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Centroid)
		{
			SelectionArr = TransformSelectionFacade.SelectCentroidInBox(InBox, InTransform);
		}

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionInSphereDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const FSphere& InSphere = GetValue<FSphere>(Context, &Sphere);
		const FTransform& InTransform = GetValue<FTransform>(Context, &Transform);

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);

		TArray<int32> SelectionArr;
		if (Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Vertices)
		{
			SelectionArr = TransformSelectionFacade.SelectVerticesInSphere(InSphere, InTransform, bAllVerticesMustContainedInSphere);
		}
		else if (Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_BoundingBox)
		{
			SelectionArr = TransformSelectionFacade.SelectBoundingBoxInSphere(InSphere, InTransform);
		}
		else if (Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Centroid)
		{
			SelectionArr = TransformSelectionFacade.SelectCentroidInSphere(InSphere, InTransform);
		}

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionTransformSelectionByFloatAttrDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		float InMin = GetValue<float>(Context, &Min);
		float InMax = GetValue<float>(Context, &Max);
		bool bInsideRange = RangeSetting == ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectByFloatAttribute(GroupName, AttrName, InMin, InMax, bInclusive, bInsideRange);

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FSelectFloatArrayIndicesInRangeDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Indices))
	{
		const TArray<float>& InValues = GetValue(Context, &Values);
		float InMin = GetValue(Context, &Min);
		float InMax = GetValue(Context, &Max);
		bool bInsideRange = RangeSetting == ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

		TArray<int32> OutIndices;
		for (int32 Idx = 0; Idx < InValues.Num(); ++Idx)
		{
			const float FloatValue = InValues[Idx];

			if (bInsideRange && FloatValue > Min && FloatValue < Max)
			{
				OutIndices.Add(Idx);
			}
			else if (!bInsideRange && (FloatValue < Min || FloatValue > Max))
			{
				OutIndices.Add(Idx);
			}
			else if (bInclusive && (FloatValue == Min || FloatValue == Max))
			{
				OutIndices.Add(Idx);
			}
		}

		SetValue(Context, MoveTemp(OutIndices), &Indices);
	}
}

void FCollectionTransformSelectionByIntAttrDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowTransformSelection>(&TransformSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		int32 InMin = GetValue<int32>(Context, &Min);
		int32 InMax = GetValue<int32>(Context, &Max);
		bool bInsideRange = RangeSetting == ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

		GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
		const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectByIntAttribute(GroupName, AttrName, InMin, InMax, bInclusive, bInsideRange);

		FDataflowTransformSelection NewTransformSelection;
		NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
		NewTransformSelection.SetFromArray(SelectionArr);

		SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionVertexSelectionCustomDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowVertexSelection>(&VertexSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (InCollection.HasGroup(FGeometryCollection::VerticesGroup))
		{
			const int32 NumVertices = InCollection.NumElements(FGeometryCollection::VerticesGroup);

			FDataflowVertexSelection NewVertexSelection;
			NewVertexSelection.Initialize(NumVertices, false);

			const FString InVertexIndices = GetValue<FString>(Context, &VertexIndicies);

			TArray<FString> Indices;
			InVertexIndices.ParseIntoArray(Indices, TEXT(" "), true);

			for (FString IndexStr : Indices)
			{
				if (IndexStr.IsNumeric())
				{
					int32 Index = FCString::Atoi(*IndexStr);
					if (Index >= 0 && Index < NumVertices)
					{
						NewVertexSelection.SetSelected(Index);
					}
					else
					{
						// ERROR: INVALID INDEX
						FString ErrorStr = "Invalid specified index found.";
						UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] %s"), *ErrorStr);
					}
				}
			}

			SetValue(Context, MoveTemp(NewVertexSelection), &VertexSelection);
		}
		else
		{
			SetValue(Context, FDataflowVertexSelection(), &VertexSelection);
		}
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionFaceSelectionCustomDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowFaceSelection>(&FaceSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (InCollection.HasGroup(FGeometryCollection::FacesGroup))
		{
			const int32 NumFaces = InCollection.NumElements(FGeometryCollection::FacesGroup);

			FDataflowFaceSelection NewFaceSelection;
			NewFaceSelection.Initialize(NumFaces, false);

			const FString InFaceIndices = GetValue<FString>(Context, &FaceIndicies);

			TArray<FString> Indices;
			InFaceIndices.ParseIntoArray(Indices, TEXT(" "), true);

			for (FString& IndexStr : Indices)
			{
				if (IndexStr.IsNumeric())
				{
					const int32 Index = FCString::Atoi(*IndexStr);
					if (Index >= 0 && Index < NumFaces)
					{
						NewFaceSelection.SetSelected(Index);
					}
					else
					{
						// ERROR: INVALID INDEX
						UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] Invalid specified index found."));
					}
				}
			}

			SetValue(Context, MoveTemp(NewFaceSelection), &FaceSelection);
		}
		else
		{
			SetValue(Context, FDataflowFaceSelection(), &FaceSelection);
		}
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionSelectionConvertDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&TransformSelection))
	{
		if (IsConnected(&VertexSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
			const FDataflowVertexSelection& InVertexSelection = GetValue(Context, &VertexSelection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.ConvertVertexSelectionToTransformSelection(InVertexSelection.AsArray(), bAllElementsMustBeSelected);

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
		}
		else if (IsConnected(&FaceSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
			const FDataflowFaceSelection& InFaceSelection = GetValue(Context, &FaceSelection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.ConvertFaceSelectionToTransformSelection(InFaceSelection.AsArray(), bAllElementsMustBeSelected);

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			SetValue(Context, MoveTemp(NewTransformSelection), &TransformSelection);
		}
		else
		{
			// Passthrough
			SafeForwardInput(Context, &TransformSelection, &TransformSelection);
		}
	}
	else if (Out->IsA(&FaceSelection))
	{
		if (IsConnected(&VertexSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
			const FDataflowVertexSelection& InVertexSelection = GetValue(Context, &VertexSelection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.ConvertVertexSelectionToFaceSelection(InVertexSelection.AsArray(), bAllElementsMustBeSelected);

			FDataflowFaceSelection NewFaceSelection;
			NewFaceSelection.Initialize(InCollection.NumElements(FGeometryCollection::FacesGroup), false);
			NewFaceSelection.SetFromArray(SelectionArr);

			SetValue(Context, MoveTemp(NewFaceSelection), &FaceSelection);
		}
		else if (IsConnected(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
			const FDataflowTransformSelection& InTransformSelection = GetValue(Context, &TransformSelection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.ConvertTransformSelectionToFaceSelection(InTransformSelection.AsArray());

			FDataflowFaceSelection NewFaceSelection;
			NewFaceSelection.Initialize(InCollection.NumElements(FGeometryCollection::FacesGroup), false);
			NewFaceSelection.SetFromArray(SelectionArr);

			SetValue(Context, MoveTemp(NewFaceSelection), &FaceSelection);
		}
		else
		{
			// Passthrough
			SafeForwardInput(Context, &FaceSelection, &FaceSelection);
		}
	}
	else if (Out->IsA(&VertexSelection))
	{
		if (IsConnected(&FaceSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
			const FDataflowFaceSelection& InFaceSelection = GetValue(Context, &FaceSelection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.ConvertFaceSelectionToVertexSelection(InFaceSelection.AsArray());

			FDataflowVertexSelection NewVertexSelection;
			NewVertexSelection.Initialize(InCollection.NumElements(FGeometryCollection::VerticesGroup), false);
			NewVertexSelection.SetFromArray(SelectionArr);

			SetValue(Context, MoveTemp(NewVertexSelection), &VertexSelection);
		}
		else if (IsConnected(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
			const FDataflowTransformSelection& InTransformSelection = GetValue(Context, &TransformSelection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.ConvertTransformSelectionToVertexSelection(InTransformSelection.AsArray());

			FDataflowVertexSelection NewVertexSelection;
			NewVertexSelection.Initialize(InCollection.NumElements(FGeometryCollection::VerticesGroup), false);
			NewVertexSelection.SetFromArray(SelectionArr);

			SetValue(Context, MoveTemp(NewVertexSelection), &VertexSelection);
		}
		else
		{
			// Passthrough
			SafeForwardInput(Context, &VertexSelection, &VertexSelection);
		}
	}
	else if (Out->IsA(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}


void FCollectionFaceSelectionInvertDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowFaceSelection>(&FaceSelection))
	{
		FDataflowFaceSelection InFaceSelection = GetValue<FDataflowFaceSelection>(Context, &FaceSelection);

		InFaceSelection.Invert();

		SetValue<FDataflowFaceSelection>(Context, MoveTemp(InFaceSelection), &FaceSelection);
	}
}


void FCollectionVertexSelectionByPercentageDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowVertexSelection>(&VertexSelection))
	{
		FDataflowVertexSelection InVertexSelection = GetValue<FDataflowVertexSelection>(Context, &VertexSelection);

		int32 InPercentage = GetValue<int32>(Context, &Percentage);
		float InRandomSeed = GetValue<float>(Context, &RandomSeed);

		TArray<int32> SelectionArr = InVertexSelection.AsArray();

		GeometryCollection::Facades::FCollectionTransformSelectionFacade::SelectByPercentage(SelectionArr, InPercentage, bDeterministic, InRandomSeed);

		InVertexSelection.SetFromArray(SelectionArr);
		SetValue(Context, MoveTemp(InVertexSelection), &VertexSelection);
	}
}


void FCollectionVertexSelectionSetOperationDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowVertexSelection>(&VertexSelection))
	{
		const FDataflowVertexSelection& InVertexSelectionA = GetValue<FDataflowVertexSelection>(Context, &VertexSelectionA);
		const FDataflowVertexSelection& InVertexSelectionB = GetValue<FDataflowVertexSelection>(Context, &VertexSelectionB);

		FDataflowVertexSelection NewVertexSelection;

		if (InVertexSelectionA.Num() == InVertexSelectionB.Num())
		{
			if (Operation == ESetOperationEnum::Dataflow_SetOperation_AND)
			{
				InVertexSelectionA.AND(InVertexSelectionB, NewVertexSelection);
			}
			else if (Operation == ESetOperationEnum::Dataflow_SetOperation_OR)
			{
				InVertexSelectionA.OR(InVertexSelectionB, NewVertexSelection);
			}
			else if (Operation == ESetOperationEnum::Dataflow_SetOperation_XOR)
			{
				InVertexSelectionA.XOR(InVertexSelectionB, NewVertexSelection);
			}
			else if (Operation == ESetOperationEnum::Dataflow_SetOperation_Subtract)
			{
				InVertexSelectionA.Subtract(InVertexSelectionB, NewVertexSelection);
			}
		}
		else
		{
			// ERROR: INPUT TRANSFORMSELECTIONS HAVE DIFFERENT NUMBER OF ELEMENTS
			FString ErrorStr = "Input VertexSelections have different number of elements.";
			UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] %s"), *ErrorStr);
		}

		SetValue(Context, MoveTemp(NewVertexSelection), &VertexSelection);
	}
}

static void CreateSelectionFromAttr(const FManagedArrayCollection& InCollection,
	const FName InGroup,
	const FName InAttribute,
	const FString InValue,
	const ESelectionByAttrOperation InOperation,
	FDataflowSelection& OutSelection)
{
	const FManagedArrayCollection::EArrayType ArrayType = InCollection.GetAttributeType(InAttribute, InGroup);
	const int32 NumElements = InCollection.NumElements(InGroup);

	if (ArrayType == FManagedArrayCollection::EArrayType::FFloatType)
	{
		const TManagedArray<float>* const Array = InCollection.FindAttributeTyped<float>(InAttribute, InGroup);
		if (InValue.IsNumeric())
		{
			float FloatValue = FCString::Atof(*InValue);

			for (int32 Idx = 0; Idx < NumElements; ++Idx)
			{
				if ((InOperation == ESelectionByAttrOperation::Equal && (*Array)[Idx] == FloatValue) ||
					(InOperation == ESelectionByAttrOperation::NotEqual && (*Array)[Idx] != FloatValue) ||
					(InOperation == ESelectionByAttrOperation::Greater && (*Array)[Idx] > FloatValue) ||
					(InOperation == ESelectionByAttrOperation::GreaterOrEqual && (*Array)[Idx] >= FloatValue) ||
					(InOperation == ESelectionByAttrOperation::Smaller && (*Array)[Idx] < FloatValue) ||
					(InOperation == ESelectionByAttrOperation::SmallerOrEqual && (*Array)[Idx] <= FloatValue))
				{
					OutSelection.SetSelected(Idx);
				}
			}
		}
		else
		{
			// Error: Invalid Value specified
			return;
		}
	}
	else if (ArrayType == FManagedArrayCollection::EArrayType::FInt32Type)
	{
		const TManagedArray<int32>* const Array = InCollection.FindAttributeTyped<int32>(InAttribute, InGroup);
		if (InValue.IsNumeric())
		{
			float IntValue = FCString::Atoi(*InValue);

			for (int32 Idx = 0; Idx < NumElements; ++Idx)
			{
				if ((InOperation == ESelectionByAttrOperation::Equal && (*Array)[Idx] == IntValue) ||
					(InOperation == ESelectionByAttrOperation::NotEqual && (*Array)[Idx] != IntValue) ||
					(InOperation == ESelectionByAttrOperation::Greater && (*Array)[Idx] > IntValue) ||
					(InOperation == ESelectionByAttrOperation::GreaterOrEqual && (*Array)[Idx] >= IntValue) ||
					(InOperation == ESelectionByAttrOperation::Smaller && (*Array)[Idx] < IntValue) ||
					(InOperation == ESelectionByAttrOperation::SmallerOrEqual && (*Array)[Idx] <= IntValue))
				{
					OutSelection.SetSelected(Idx);
				}
			}
		}
		else
		{
			// Error: Invalid Value specified
			return;
		}
	}
	else if (ArrayType == FManagedArrayCollection::EArrayType::FStringType)
	{
		const TManagedArray<FString>* const Array = InCollection.FindAttributeTyped<FString>(InAttribute, InGroup);

		for (int32 Idx = 0; Idx < NumElements; ++Idx)
		{
			if ((InOperation == ESelectionByAttrOperation::Equal && (*Array)[Idx] == InValue) ||
				(InOperation == ESelectionByAttrOperation::NotEqual && !((*Array)[Idx] == InValue)))
			{
				OutSelection.SetSelected(Idx);
			}
		}
	}
	else if (ArrayType == FManagedArrayCollection::EArrayType::FBoolType)
	{
		const TManagedArray<bool>* const Array = InCollection.FindAttributeTyped<bool>(InAttribute, InGroup);
		bool BoolValue = false;
		if (InValue.IsNumeric())
		{
			float FloatValue = FCString::Atof(*InValue);

			if (FloatValue > 0.f)
			{
				BoolValue = true;
			}
		}
		else
		{
			if (InValue == FString("true") || InValue == FString("True"))
			{
				BoolValue = true;
			}
		}

		for (int32 Idx = 0; Idx < NumElements; ++Idx)
		{
			if ((InOperation == ESelectionByAttrOperation::Equal && (*Array)[Idx] == BoolValue) ||
				(InOperation == ESelectionByAttrOperation::NotEqual && !((*Array)[Idx] == BoolValue)))
			{
				OutSelection.SetSelected(Idx);
			}
		}
	}
}


void FCollectionSelectionByAttrDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowVertexSelection>(&VertexSelection) ||
		Out->IsA<FDataflowFaceSelection>(&FaceSelection) ||
		Out->IsA<FDataflowTransformSelection>(&TransformSelection) ||
		Out->IsA<FDataflowGeometrySelection>(&GeometrySelection) ||
		Out->IsA<FDataflowMaterialSelection>(&MaterialSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FCollectionAttributeKey InAttributeKey = GetValue<FCollectionAttributeKey>(Context, &AttributeKey);
		FName GroupName = UE::Dataflow::Private::GetAttributeFromEnumAsName(Group);
		FName AttributeName = FName(Attribute);
		if (IsConnected(&AttributeKey))
		{
			GroupName = FName(InAttributeKey.Group);
			AttributeName = FName(InAttributeKey.Attribute);
		}

		if (InCollection.HasGroup(GroupName))
		{
			if (InCollection.HasAttribute(AttributeName, GroupName))
			{
				const int32 NumFaces = InCollection.NumElements(GroupName);

				FDataflowSelection NewSelection;
				NewSelection.Initialize(NumFaces, false);

				CreateSelectionFromAttr(InCollection,
					GroupName,
					AttributeName,
					Value,
					Operation,
					NewSelection);

				SetValue(Context, GroupName == UE::Dataflow::Private::GetAttributeFromEnumAsName(ESelectionByAttrGroup::Vertices) ? MoveTemp(NewSelection) : FDataflowSelection(), &VertexSelection);
				SetValue(Context, GroupName == UE::Dataflow::Private::GetAttributeFromEnumAsName(ESelectionByAttrGroup::Faces) ? MoveTemp(NewSelection) : FDataflowSelection(), &FaceSelection);
				SetValue(Context, GroupName == UE::Dataflow::Private::GetAttributeFromEnumAsName(ESelectionByAttrGroup::Transform) ? MoveTemp(NewSelection) : FDataflowSelection(), &TransformSelection);
				SetValue(Context, GroupName == UE::Dataflow::Private::GetAttributeFromEnumAsName(ESelectionByAttrGroup::Geometry) ? MoveTemp(NewSelection) : FDataflowSelection(), &GeometrySelection);
				SetValue(Context, GroupName == UE::Dataflow::Private::GetAttributeFromEnumAsName(ESelectionByAttrGroup::Material) ? MoveTemp(NewSelection) : FDataflowSelection(), &MaterialSelection);

				return;
			}
		}

		SetValue(Context, FDataflowSelection(), &VertexSelection);
		SetValue(Context, FDataflowSelection(), &FaceSelection);
		SetValue(Context, FDataflowSelection(), &TransformSelection);
		SetValue(Context, FDataflowSelection(), &GeometrySelection);
		SetValue(Context, FDataflowSelection(), &MaterialSelection);
	}
	else if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FGeometrySelectionToVertexSelectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FDataflowVertexSelection>(&VertexSelection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		const int32 NumVertices = InCollection.NumElements(FGeometryCollection::VerticesGroup);
		const int32 NumGeometries = InCollection.NumElements(FGeometryCollection::GeometryGroup);
		
		FDataflowVertexSelection InVertexSelection;
		InVertexSelection.Initialize(NumVertices, false);
		const TManagedArray<int32>* VertexStart = InCollection.FindAttributeTyped<int32>("VertexStart", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>* VertexCount = InCollection.FindAttributeTyped<int32>("VertexCount", FGeometryCollection::GeometryGroup);
		TArray<int32> InGeometryIndexArray;
		if (IsConnected(&GeometrySelection))
		{
			InGeometryIndexArray = GetValue<FDataflowGeometrySelection>(Context, &GeometrySelection).AsArray();
		}
		else
		{
			const FString InGeometryIndices = GetValue<FString>(Context, &GeometryIndices);
			TArray<FString> Indices;
			InGeometryIndices.ParseIntoArray(Indices, TEXT(" "), true);
			for (FString IndexStr : Indices)
			{
				if (IndexStr.IsNumeric())
				{
					int32 Index = FCString::Atoi(*IndexStr);
					if (Index >= 0 && Index < NumGeometries)
					{
						InGeometryIndexArray.Add(Index);
					}
					else
					{
						// ERROR: INVALID INDEX
						FString ErrorStr = "Invalid geometry index found.";
						UE_LOG(LogTemp, Error, TEXT("[Dataflow ERROR] %s"), *ErrorStr);
					}
				}
			}
		}
		if (VertexStart && VertexCount)
		{
			TArray<int32> VertexIndices;
			for (int32 GeometryIdx : InGeometryIndexArray)
			{
				if (ensure(VertexStart->IsValidIndex(GeometryIdx)))
				{
					const int32 Start = (*VertexStart)[GeometryIdx];
					const int32 Count = (*VertexCount)[GeometryIdx];
					for (int32 VertexIdx = Start; VertexIdx < Start + Count; ++VertexIdx)
					{
						VertexIndices.Add(VertexIdx);
					}
				}
			}
			InVertexSelection.SetFromArray(VertexIndices);
		}
		SetValue(Context, MoveTemp(InVertexSelection), &VertexSelection);
	}
}