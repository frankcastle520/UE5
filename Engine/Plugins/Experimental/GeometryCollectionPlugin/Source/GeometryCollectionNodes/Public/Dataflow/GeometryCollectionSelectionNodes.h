// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowSelection.h"
#include "Math/MathFwd.h"
#include "Math/Sphere.h"
#include "Dataflow/DataflowConnectionTypes.h"

#include "GeometryCollectionSelectionNodes.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

class FGeometryCollection;


/**
 *
 * Selects all the bones for the Collection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionAllDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionAllDataflowNode, "CollectionTransformSelectAll", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionAllDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class ESetOperationEnum : uint8
{
	/** Select elements that are selected in both incoming selections (Bitwise AND) */
	Dataflow_SetOperation_AND UMETA(DisplayName = "Intersect"),
	/** Select elements that are selected in either incoming selections (Bitwise OR) */
	Dataflow_SetOperation_OR UMETA(DisplayName = "Union"),
	/** Select elements that are selected in exactly one incoming selection (Bitwise XOR) */
	Dataflow_SetOperation_XOR UMETA(DisplayName = "Symmetric Difference (XOR)"),
	/** Select elements that are selected in only the first of the incoming selections (Bitwise A AND (NOT B)) */
	Dataflow_SetOperation_Subtract UMETA(DisplayName = "Difference"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Runs boolean operation on TransformSelections
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionSetOperationDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionSetOperationDataflowNode, "CollectionTransformSelectionSetOperation", "GeometryCollection|Selection|Transform", "")

public:
	/** Boolean operation */
	UPROPERTY(EditAnywhere, Category = "Compare");
	ESetOperationEnum Operation = ESetOperationEnum::Dataflow_SetOperation_AND;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelectionA", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelectionA;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelectionB", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelectionB;

	/** Array of the selected bone indices after operation*/
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelectionA"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionSetOperationDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelectionA);
		RegisterInputConnection(&TransformSelectionB);
		RegisterOutputConnection(&TransformSelection, &TransformSelectionA);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates a formatted string of the bones and the selection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionInfoDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionInfoDataflowNode, "CollectionTransformSelectionInfo", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DisplayName = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** Formatted string of the bones and selection */
	UPROPERTY(meta = (DataflowOutput))
	FString String;

	FCollectionTransformSelectionInfoDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&String);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Generates an empty bone selection for the Collection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionNoneDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionNoneDataflowNode, "CollectionTransformSelectNone", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionNoneDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Inverts selection of bones
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionInvertDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionInvertDataflowNode, "CollectionTransformSelectInvert", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionInvertDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects bones randomly in the Collection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionRandomDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionRandomDataflowNode, "CollectionTransformSelectRandom", "GeometryCollection|Selection|Transform", "")

public:
	/** If true, it always generates the same result for the same RandomSeed */
	UPROPERTY(EditAnywhere, Category = "Random")
	bool bDeterministic = false;

	/** Seed for the random generation, only used if Deterministic is on */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, EditCondition = "bDeterministic"))
	float RandomSeed = 0.f;

	/** Bones get selected if RandomValue > RandomThreshold */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, UIMin = 0.f, UIMax = 1.f))
	float RandomThreshold = 0.5f;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionRandomDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::FRandRange(-1e5, 1e5);
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&RandomSeed);
		RegisterInputConnection(&RandomThreshold);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the root bones in the Collection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionRootDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionRootDataflowNode, "CollectionTransformSelectRoot", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionRootDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 * 
 * Selects specified bones in the GeometryCollection by using a 
 * space separated list
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionCustomDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionCustomDataflowNode, "CollectionTransformSelectCustom", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Space separated list of bone indices to specify the selection */
	UPROPERTY(EditAnywhere, Category = "Selection", meta=(DisplayName="Bone Indices"))
	FString BoneIndicies = FString(); //Fix typo for v2

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionCustomDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoneIndicies);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

/**
 *
 * Convert index array to a transform selection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionFromIndexArrayDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionFromIndexArrayDataflowNode, "CollectionTransformSelectionFromIndexArray", "GeometryCollection|Selection|Array", "")

public:

	/** Collection to use for the selection. Note only valid bone indices for the collection will be included in the output selection. */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of bone indices to convert to a trannsform selection */
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (DataflowInput))
	TArray<int32> BoneIndices;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionFromIndexArrayDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&BoneIndices);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the parents of the currently selected bones
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionParentDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionParentDataflowNode, "CollectionTransformSelectParent", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionParentDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Outputs the specified percentage of the selected bones
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionByPercentageDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionByPercentageDataflowNode, "CollectionTransformSelectByPercentage", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** Percentage to keep from the original selection */
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (UIMin = 0, UIMax = 100))
	int32 Percentage = 100;

	/** Sets the random generation to deterministic */
	UPROPERTY(EditAnywhere, Category = "Random")
	bool bDeterministic = false;

	/** Seed value for the random generation */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, EditCondition = "bDeterministic"))
	float RandomSeed = 0.f;

	FCollectionTransformSelectionByPercentageDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::RandRange(-100000, 100000);
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Percentage);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the children of the selected bones
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionChildrenDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionChildrenDataflowNode, "CollectionTransformSelectChildren", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionChildrenDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the siblings of the selected bones
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionSiblingsDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionSiblingsDataflowNode, "CollectionTransformSelectSiblings", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionSiblingsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Expand the selection to include all nodes with the same level as the selected nodes
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionLevelDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionLevelDataflowNode, "CollectionTransformSelectLevels", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	FCollectionTransformSelectionLevelDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the root bones in the Collection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionTargetLevelDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionTargetLevelDataflowNode, "CollectionTransformSelectTargetLevel", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Level to select */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DataflowInput, ClampMin = 0))
	int32 TargetLevel = 1;

	/** Whether to avoid embedded geometry in the selection (i.e., only select rigid and cluster nodes) */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bSkipEmbedded = false;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionTargetLevelDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&TargetLevel);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};



/**
 *
 * Selects the contact(s) of the selected bones
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionContactDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionContactDataflowNode, "CollectionTransformSelectContact", "GeometryCollection|Selection|Transform", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "TransformSelection", DataflowPassthrough = "TransformSelection", DataflowIntrinsic))
	FDataflowTransformSelection TransformSelection;

	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Whether to allow contact with bones that are in a parent level */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bAllowContactInParentLevels = true;

	FCollectionTransformSelectionContactDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&TransformSelection);
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the leaves in the Collection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionLeafDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionLeafDataflowNode, "CollectionTransformSelectLeaf", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionLeafDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the clusters in the Collection
 * Deprecated : this node had the wrong behavior and select the leaves instead
 *				Replace it by CollectionTransformSelectLeaf or use the second version of CollectionTransformSelectCluster
 *
 */
USTRUCT(meta = (Deprecated = "5.5"))
struct FCollectionTransformSelectionClusterDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionClusterDataflowNode, "CollectionTransformSelectCluster", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionClusterDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects the clusters in the Collection
 * this version works properly and address the issues found in the deprecated version 1
 */
USTRUCT()
struct FCollectionTransformSelectionClusterDataflowNode_v2 : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionClusterDataflowNode_v2, "CollectionTransformSelectCluster", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionClusterDataflowNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class ERangeSettingEnum : uint8
{
	/** Values for selection must be inside of the specified range */
	Dataflow_RangeSetting_InsideRange UMETA(DisplayName = "Inside Range"),
	/** Values for selection must be outside of the specified range */
	Dataflow_RangeSetting_OutsideRange UMETA(DisplayName = "Outside Range"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};


/**
 *
 * Selects indices of a float array by range
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FSelectFloatArrayIndicesInRangeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FSelectFloatArrayIndicesInRangeDataflowNode, "SelectFloatArrayIndicesInRange", "GeometryCollection|Selection|Array", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput))
	TArray<float> Values;

	/** Minimum value for the selection */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float Min = 0.f;

	/** Maximum value for the selection */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float Max = 1000.f;

	/** Values for the selection has to be inside or outside [Min, Max] range */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	ERangeSettingEnum RangeSetting = ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

	/** If true then range includes Min and Max values */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	bool bInclusive = true;

	/** Indices of float Values matching the specified range */
	UPROPERTY(meta = (DataflowOutput))
	TArray<int> Indices;

	FSelectFloatArrayIndicesInRangeDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Values);
		RegisterInputConnection(&Min);
		RegisterInputConnection(&Max);
		RegisterOutputConnection(&Indices);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects pieces based on their size
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionBySizeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionBySizeDataflowNode, "CollectionTransformSelectBySize", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Minimum size for the selection */
	UPROPERTY(EditAnywhere, Category = "Size", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float SizeMin = 0.f;

	/** Maximum size for the selection */
	UPROPERTY(EditAnywhere, Category = "Size", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float SizeMax = 1000.f;

	/** Values for the selection has to be inside or outside [Min, Max] range */
	UPROPERTY(EditAnywhere, Category = "Size")
	ERangeSettingEnum RangeSetting = ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

	/** If true then range includes Min and Max values */
	UPROPERTY(EditAnywhere, Category = "Size")
	bool bInclusive = true;

	/** Whether to use the 'Relative Size' -- i.e., the Size / Largest Bone Size. Otherwise, Size is the cube root of Volume. */
	UPROPERTY(EditAnywhere, Category = "Size")
	bool bUseRelativeSize = true;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionBySizeDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&SizeMin);
		RegisterInputConnection(&SizeMax);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects pieces based on their volume
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionByVolumeDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionByVolumeDataflowNode, "CollectionTransformSelectByVolume", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Minimum volume for the selection */
	UPROPERTY(EditAnywhere, Category = "Volume", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float VolumeMin = 0.f;

	/** Maximum volume for the selection */
	UPROPERTY(EditAnywhere, Category = "Volume", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float VolumeMax = 1000.f;

	/** Values for the selection has to be inside or outside [Min, Max] range */
	UPROPERTY(EditAnywhere, Category = "Volume")
	ERangeSettingEnum RangeSetting = ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

	/** If true then range includes Min and Max values */
	UPROPERTY(EditAnywhere, Category = "Volume")
	bool bInclusive = true;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionByVolumeDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&VolumeMin);
		RegisterInputConnection(&VolumeMax);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


UENUM(BlueprintType)
enum class ESelectSubjectTypeEnum : uint8
{
	/** InBox must contain the vertices of the bone */
	Dataflow_SelectSubjectType_Vertices UMETA(DisplayName = "Vertices"),
	/** InBox must contain the BoundingBox of the bone */
	Dataflow_SelectSubjectType_BoundingBox UMETA(DisplayName = "BoundingBox"),
	/** InBox must contain the centroid of the bone */
	Dataflow_SelectSubjectType_Centroid UMETA(DisplayName = "Centroid"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Selects bones if their Vertices/BoundingBox/Centroid in a box
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionInBoxDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionInBoxDataflowNode, "CollectionTransformSelectInBox", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Box to contain Vertices/BoundingBox/Centroid */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FBox Box = FBox(ForceInit);

	/** Transform for the box */
	UPROPERTY(EditAnywhere, Category = "Select")
	FTransform Transform;

	/** Subject (Vertices/BoundingBox/Centroid) to check against box */
	UPROPERTY(EditAnywhere, Category = "Select", DisplayName = "Type to Check in Box")
	ESelectSubjectTypeEnum Type = ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Centroid;

	/** If true all the vertices of the piece must be inside of box */
	UPROPERTY(EditAnywhere, Category = "Select", meta = (EditCondition = "Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Vertices"))
	bool bAllVerticesMustContainedInBox = true;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionInBoxDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Box);
		RegisterInputConnection(&Transform);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects bones if their Vertices/BoundingBox/Centroid in a sphere
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionInSphereDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionInSphereDataflowNode, "CollectionTransformSelectInSphere", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Sphere to contain Vertices/BoundingBox/Centroid */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	FSphere Sphere = FSphere(ForceInit);

	/** Transform for the sphere */
	UPROPERTY(EditAnywhere, Category = "Select")
	FTransform Transform;

	/** Subject (Vertices/BoundingBox/Centroid) to check against box */
	UPROPERTY(EditAnywhere, Category = "Select", DisplayName = "Type to Check in Sphere")
	ESelectSubjectTypeEnum Type = ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Centroid;

	/** If true all the vertices of the piece must be inside of box */
	UPROPERTY(EditAnywhere, Category = "Select", meta = (EditCondition = "Type == ESelectSubjectTypeEnum::Dataflow_SelectSubjectType_Vertices"))
	bool bAllVerticesMustContainedInSphere = true;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionInSphereDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Sphere);
		RegisterInputConnection(&Transform);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects bones by a float attribute
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionByFloatAttrDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionByFloatAttrDataflowNode, "CollectionTransformSelectByFloatAttribute", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Group name for the attr */
	UPROPERTY(VisibleAnywhere, Category = "Attribute", meta = (DisplayName = "Group"))
	FString GroupName = FString("Transform");

	/** Attribute name */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (DisplayName = "Attribute"))
	FString AttrName = FString("");

	/** Minimum value for the selection */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float Min = 0.f;

	/** Maximum value for the selection */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (UIMin = 0.f, UIMax = 1000000000.f))
	float Max = 1000.f;

	/** Values for the selection has to be inside or outside [Min, Max] range */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	ERangeSettingEnum RangeSetting = ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

	/** If true then range includes Min and Max values */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	bool bInclusive = true;

	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionByFloatAttrDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Min);
		RegisterInputConnection(&Max);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects bones by an int attribute
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionTransformSelectionByIntAttrDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionTransformSelectionByIntAttrDataflowNode, "CollectionTransformSelectByIntAttribute", "GeometryCollection|Selection|Transform", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Group name for the attr */
	UPROPERTY(VisibleAnywhere, Category = "Attribute", meta = (DisplayName = "Group"))
	FString GroupName = FString("Transform");

	/** Attribute name */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (DisplayName = "Attribute"))
	FString AttrName = FString("");

	/** Minimum value for the selection */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (UIMin = 0, UIMax = 1000000000))
	int32 Min = 0;

	/** Maximum value for the selection */
	UPROPERTY(EditAnywhere, Category = "Attribute", meta = (UIMin = 0, UIMax = 1000000000))
	int32 Max = 1000;

	/** Values for the selection has to be inside or outside [Min, Max] range */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	ERangeSettingEnum RangeSetting = ERangeSettingEnum::Dataflow_RangeSetting_InsideRange;

	/** If true then range includes Min and Max values */
	UPROPERTY(EditAnywhere, Category = "Attribute")
	bool bInclusive = true;

	/** Transform selection including the new indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	FCollectionTransformSelectionByIntAttrDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&Min);
		RegisterInputConnection(&Max);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects specified vertices in the GeometryCollection by using a
 * space separated list
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionVertexSelectionCustomDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionVertexSelectionCustomDataflowNode, "CollectionVertexSelectCustom", "GeometryCollection|Selection|Vertex", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Space separated list of vertex indices to specify the selection */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FString VertexIndicies = FString(); //Fix typo for v2

	/** Vertex selection including the new indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	FCollectionVertexSelectionCustomDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&VertexIndicies);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&VertexSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Selects specified faces in the GeometryCollection by using a
 * space separated list
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionFaceSelectionCustomDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionFaceSelectionCustomDataflowNode, "CollectionFaceSelectCustom", "GeometryCollection|Selection|Face", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Space separated list of face indices to specify the selection */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FString FaceIndicies = FString(); //Fix typo for v2

	/** Face selection including the new indices */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FaceSelection"))
	FDataflowFaceSelection FaceSelection;

	FCollectionFaceSelectionCustomDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&FaceIndicies);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&FaceSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Converts Vertex/Face/Transform selection into Vertex/Face/Transform selection
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionSelectionConvertDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionSelectionConvertDataflowNode, "CollectionSelectionConvert", "GeometryCollection|Selection", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** Transform selection including the new indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "TransformSelection", DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Face selection including the new indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "FaceSelection", DisplayName = "FaceSelection"))
	FDataflowFaceSelection FaceSelection;

	/** Vertex selection including the new indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "VertexSelection", DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;
	
	/** If true then for converting vertex/face selection to transform selection all vertex/face must be selected for selecting the associated transform */
	UPROPERTY(EditAnywhere, Category = "Selection")
	bool bAllElementsMustBeSelected = false;

	FCollectionSelectionConvertDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&FaceSelection);
		RegisterInputConnection(&TransformSelection);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&TransformSelection, &TransformSelection);
		RegisterOutputConnection(&FaceSelection, &FaceSelection);
		RegisterOutputConnection(&VertexSelection, &VertexSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Inverts selection of faces
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionFaceSelectionInvertDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionFaceSelectionInvertDataflowNode, "CollectionFaceSelectInvert", "GeometryCollection|Selection|Face", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "FaceSelection", DataflowPassthrough = "FaceSelection", DataflowIntrinsic))
	FDataflowFaceSelection FaceSelection;

	FCollectionFaceSelectionInvertDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&FaceSelection);
		RegisterOutputConnection(&FaceSelection, &FaceSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Outputs the specified percentage of the selected vertices
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionVertexSelectionByPercentageDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionVertexSelectionByPercentageDataflowNode, "CollectionVertexSelectByPercentage", "GeometryCollection|Selection|Vertex", "")

public:
	/** Array of the selected bone indices */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "VertexSelection", DataflowPassthrough = "VertexSelection", DataflowIntrinsic))
	FDataflowVertexSelection VertexSelection;

	/** Percentage to keep from the original selection */
	UPROPERTY(EditAnywhere, Category = "Selection", meta = (UIMin = 0, UIMax = 100))
	int32 Percentage = 100;

	/** Sets the random generation to deterministic */
	UPROPERTY(EditAnywhere, Category = "Random")
	bool bDeterministic = false;

	/** Seed value for the random generation */
	UPROPERTY(EditAnywhere, Category = "Random", meta = (DataflowInput, EditCondition = "bDeterministic"))
	float RandomSeed = 0.f;

	FCollectionVertexSelectionByPercentageDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RandomSeed = FMath::RandRange(-100000, 100000);
		RegisterInputConnection(&VertexSelection);
		RegisterInputConnection(&Percentage);
		RegisterInputConnection(&RandomSeed);
		RegisterOutputConnection(&VertexSelection, &VertexSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};


/**
 *
 * Runs boolean operation on VertexSelections
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionVertexSelectionSetOperationDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionVertexSelectionSetOperationDataflowNode, "CollectionVertexSelectionSetOperation", "GeometryCollection|Selection|Vertex", "")

public:
	/** Boolean operation */
	UPROPERTY(EditAnywhere, Category = "Compare");
	ESetOperationEnum Operation = ESetOperationEnum::Dataflow_SetOperation_AND;

	/** Array of the selected vertex indices */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelectionA", DataflowIntrinsic))
	FDataflowVertexSelection VertexSelectionA;

	/** Array of the selected vertex indices */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelectionB", DataflowIntrinsic))
	FDataflowVertexSelection VertexSelectionB;

	/** Array of the selected vertex indices after operation */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "VertexSelection", DataflowPassthrough = "VertexSelectionA"))
	FDataflowVertexSelection VertexSelection;

	FCollectionVertexSelectionSetOperationDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&VertexSelectionA);
		RegisterInputConnection(&VertexSelectionB);
		RegisterOutputConnection(&VertexSelection, &VertexSelectionA);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};

UENUM(BlueprintType)
enum class ESelectionByAttrGroup : uint8
{
	Vertices UMETA(DisplayName = "Vertices"),
	Faces UMETA(DisplayName = "Faces"),
	Transform UMETA(DisplayName = "Transform"),
	Geometry UMETA(DisplayName = "Geometry"),
	Material UMETA(DisplayName = "Material")
};

namespace UE::Dataflow::Private
{
	inline FName GetAttributeFromEnumAsName(const ESelectionByAttrGroup Value)
	{
		static const UEnum* SelectionByAttrGroupEnum = StaticEnum<ESelectionByAttrGroup>();
		return *SelectionByAttrGroupEnum->GetNameStringByValue((int64)Value);
	}
}

UENUM(BlueprintType)
enum class ESelectionByAttrOperation : uint8
{
	/** Select faces which attribute value equal with specified value */
	Equal UMETA(DisplayName = "=="),
	/** Select faces which attribute value not equal with specified value */
	NotEqual UMETA(DisplayName = "!="),
	/** Select faces which attribute value greater than specified value */
	Greater UMETA(DisplayName = ">"),
	/** Select faces which attribute value greater or equal than specified value */
	GreaterOrEqual UMETA(DisplayName = ">="),
	/** Select faces which attribute value smaller than specified value */
	Smaller UMETA(DisplayName = "<"),
	/** Select faces which attribute value greater than specified value */
	SmallerOrEqual UMETA(DisplayName = "<=")
};

/**
 *
 * Selects specified Vertices/Faces/Transforms in the GeometryCollection by using an attribute value
 * Currently supported attribute types: float, int32, String, bool
 *
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FCollectionSelectionByAttrDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FCollectionSelectionByAttrDataflowNode, "CollectionSelectByAttr", "GeometryCollection|Selection|All", "")

public:
	/** GeometryCollection for the selection */
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection", DataflowIntrinsic))
	FManagedArrayCollection Collection;

	/** AttributeKey input */
	UPROPERTY(meta = (DataflowInput))
	FCollectionAttributeKey AttributeKey;

	/** Group */
	UPROPERTY(EditAnywhere, Category = "Selection")
	ESelectionByAttrGroup Group = ESelectionByAttrGroup::Faces;

	/** Attribute for the selection */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FString Attribute = FString("Internal");

	/** Operation */
	UPROPERTY(EditAnywhere, Category = "Selection")
	ESelectionByAttrOperation Operation = ESelectionByAttrOperation::Equal;

	/** Attribute value for the operation */
	UPROPERTY(EditAnywhere, Category = "Selection")
	FString Value = FString("true");

	/** Vertex selection output */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	/** Face selection output */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "FaceSelection"))
	FDataflowFaceSelection FaceSelection;

	/** Transform selection output */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "TransformSelection"))
	FDataflowTransformSelection TransformSelection;

	/** Geometry selection output */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "GeometrySelection"))
	FDataflowGeometrySelection GeometrySelection;

	/** Material selection output */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "MaterialSelection"))
	FDataflowMaterialSelection MaterialSelection;

	FCollectionSelectionByAttrDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&AttributeKey);
		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&VertexSelection);
		RegisterOutputConnection(&FaceSelection);
		RegisterOutputConnection(&TransformSelection);
		RegisterOutputConnection(&GeometrySelection);
		RegisterOutputConnection(&MaterialSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * Converts GeometrySelection to VertexSelection
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGeometrySelectionToVertexSelectionDataflowNode final : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGeometrySelectionToVertexSelectionDataflowNode, "GeometrySelectionToVertexSelection", "GeometryCollection|Selection|All", "")

public:
	/** GeometryCollection */
	UPROPERTY(meta = (DataflowInput))
	FManagedArrayCollection Collection;

	/** Space separated list of geometry indices to specify the selection when GeometrySelection is not connected*/
	UPROPERTY(EditAnywhere, Category = "Selection")
	FString GeometryIndices = FString();

	/** Input geometry selection */
	UPROPERTY(meta = (DataflowInput, DisplayName = "GeometrySelection"))
	FDataflowGeometrySelection GeometrySelection;

	/** Vertex selection output */
	UPROPERTY(meta = (DataflowOutput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	FGeometrySelectionToVertexSelectionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&GeometrySelection);
		RegisterOutputConnection(&VertexSelection);
	}

private:
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

namespace UE::Dataflow
{
	void GeometryCollectionSelectionNodes();
}

