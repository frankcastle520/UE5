// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Chaos/BoundingVolumeHierarchy.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowConnectionTypes.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Dataflow/DataflowSelection.h"

#include "GeometryCollectionTransferVertexAttributeNode.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

UENUM(BlueprintType)
enum class EDataflowTransferVertexAttributeNodeFalloff : uint8
{
	/** Squared falloff based on distance from triangle*/
	Squared  UMETA(DisplayName = "Squared"),

	/** Linear falloff based on distance from triangle*/
	Linear UMETA(DisplayName = "Linear"),

	/** No distance falloff */
	None UMETA(DisplayName = "None"),
	//~~~
	//256th entry
	Dataflow_Max UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EDataflowTransferVertexAttributeNodeSourceScale : uint8
{
	/** Bounding volume hierarchy cell size based on max edge length of each geometry group*/
	Component_Edge  UMETA(DisplayName = "Component Max Edge"),

	/** Bounding volume hierarchy cell size based on max edge length of the whole asset*/
	Asset_Edge UMETA(DisplayName = "Asset Max Edge"),

	/** Bounding volume hierarchy cell size based on max length of the bounding box of the whole asset*/
	Asset_Bound UMETA(DisplayName = "Asset Max Bound"),

	//~~~
	//256th entry
	Dataflow_Max UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EDataflowTransferVertexAttributeNodeBoundingVolume : uint8
{
	/** Bounding volume on vertices of the source triangle mesh*/
	Vertex UMETA(DisplayName = "Vertex"),

	/** Bounding volume on triangles of the source triangle mesh*/
	Triangle UMETA(DisplayName = "Triangle"),

	//~~~
	//256th entry
	Dataflow_Max UMETA(Hidden)
};

/**
 * Transfer float properties from a source collection to a target collection.
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGeometryCollectionTransferVertexAttributeNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGeometryCollectionTransferVertexAttributeNode, "TransferVertexAttribute", "GeometryCollection", "Transfer a named vertex attribute from the Source Collection to the Target Collection")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceWeightsRender", FGeometryCollection::StaticType(), "Collection", "AttributeKey")

public:

	/* Target collection to transfer vertex attribute to. */
	UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/* Source collection to transfer vertex attribute from. */
	UPROPERTY(Meta = (DataflowInput, DisplayName = "FromCollection"))
	FManagedArrayCollection FromCollection;

	/* The name of the vertex attribute to generate indices from. */
	UPROPERTY(EditAnywhere, Category = "Dataflow", Meta = (DataflowInput, DataflowOutput, DisplayName = "AttributeKey", DataflowPassthrough = "AttributeKey"))
	FCollectionAttributeKey AttributeKey = FCollectionAttributeKey(FString(""), FString("Vertices"));
	
	/* Bounding volume type for source assets[default: Triangle] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	EDataflowTransferVertexAttributeNodeBoundingVolume BoundingVolumeType = EDataflowTransferVertexAttributeNodeBoundingVolume::Triangle;

	/* Bounding volume hierarchy cell size for neighboring vertices to transfer into[default: Asset] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	EDataflowTransferVertexAttributeNodeSourceScale SourceScale = EDataflowTransferVertexAttributeNodeSourceScale::Asset_Bound;

	/* Falloff of source value based on distance from source triangle[default: Squared] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	EDataflowTransferVertexAttributeNodeFalloff Falloff = EDataflowTransferVertexAttributeNodeFalloff::None;

	/* Threshold based on distance from source triangle.Values past the threshold will falloff.[Defaults to 1 percent of triangle size(0.01)] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	float FalloffThreshold = 0.01f;

	/* Edge multiplier for the Bounding Volume Hierarchy(BVH) target's particle search radius. */
	UPROPERTY(EditAnywhere, Category = "Thresholds", meta = (EditCondition = "SourceScale == EDataflowTransferVertexAttributeNodeSourceScale::Asset_Edge || SourceScale == EDataflowTransferVertexAttributeNodeSourceScale::Component_Edge", EditConditionHides))
	float EdgeMultiplier = 0.5f;

	/* Max bound multiplier for the Bounding Volume Hierarchy(BVH) target's particle search radius. */
	UPROPERTY(EditAnywhere, Category = "Thresholds", meta = (
		EditCondition = "SourceScale == EDataflowTransferVertexAttributeNodeSourceScale::Asset_Bound",
		EditConditionHides))
	float BoundMultiplier = 0.01f;

	FGeometryCollectionTransferVertexAttributeNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&FromCollection);
		RegisterInputConnection(&AttributeKey);

		RegisterOutputConnection(&Collection, &Collection);
		RegisterOutputConnection(&AttributeKey, &AttributeKey);
	}

private:

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * Transfer skin weights from a source collection to a target collection.
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGeometryCollectionTransferVertexSkinWeightsNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGeometryCollectionTransferVertexSkinWeightsNode, "TransferVertexSkinWeights", "GeometryCollection", "Transfer vertex skin weights from the Source Collection to the Target Collection")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FGeometryCollection::StaticType(), "Collection")

public:

	/* Target collection to transfer vertex attribute to. */
	UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/* Source collection to transfer vertex attribute from. */
	UPROPERTY(Meta = (DataflowInput, DisplayName = "FromCollection"))
	FManagedArrayCollection FromCollection;

	/* Bounding volume type for source assets[default: Triangle] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	EDataflowTransferVertexAttributeNodeBoundingVolume BoundingVolumeType = EDataflowTransferVertexAttributeNodeBoundingVolume::Triangle;

	/* Bounding volume hierarchy cell size for neighboring vertices to transfer into[default: Asset] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	EDataflowTransferVertexAttributeNodeSourceScale SourceScale = EDataflowTransferVertexAttributeNodeSourceScale::Asset_Bound;

	/* Falloff of source value based on distance from source triangle[default: Squared] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	EDataflowTransferVertexAttributeNodeFalloff Falloff = EDataflowTransferVertexAttributeNodeFalloff::None;

	/* Threshold based on distance from source triangle.Values past the threshold will falloff.[Defaults to 1 percent of triangle size(0.01)] */
	UPROPERTY(EditAnywhere, Category = "Thresholds")
	float FalloffThreshold = 0.01f;

	/* Edge multiplier for the Bounding Volume Hierarchy(BVH) target's particle search radius. */
	UPROPERTY(EditAnywhere, Category = "Thresholds", meta = (EditCondition = "SourceScale == EDataflowTransferVertexAttributeNodeSourceScale::Asset_Edge || SourceScale == EDataflowTransferVertexAttributeNodeSourceScale::Component_Edge", EditConditionHides))
	float EdgeMultiplier = 0.5f;

	/* Max bound multiplier for the Bounding Volume Hierarchy(BVH) target's particle search radius. */
	UPROPERTY(EditAnywhere, Category = "Thresholds", meta = (
		EditCondition = "SourceScale == EDataflowTransferVertexAttributeNodeSourceScale::Asset_Bound",
		EditConditionHides))
	float BoundMultiplier = 0.01f;

	FGeometryCollectionTransferVertexSkinWeightsNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&FromCollection);
		RegisterOutputConnection(&Collection, &Collection);
	}

private:

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

/**
 * Set VertexSelection to be kinematic. Note that kinematic particles need skin weights.
 */
USTRUCT(meta = (DataflowGeometryCollection))
struct FGeometryCollectionSetKinematicVertexSelectionNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGeometryCollectionSetKinematicVertexSelectionNode, "SetKinematicVertexSelection", "GeometryCollection", "Set Vertex Collection to be kinematic")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FGeometryCollection::StaticType(), "Collection")

public:

	UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** Vertex Selection set to be kinematic */
	UPROPERTY(meta = (DataflowInput, DisplayName = "VertexSelection"))
	FDataflowVertexSelection VertexSelection;

	FGeometryCollectionSetKinematicVertexSelectionNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&VertexSelection);
		RegisterOutputConnection(&Collection, &Collection);
	}

private:

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};