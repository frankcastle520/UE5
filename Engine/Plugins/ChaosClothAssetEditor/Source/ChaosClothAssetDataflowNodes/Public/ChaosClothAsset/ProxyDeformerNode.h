// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dataflow/DataflowNode.h"
#include "ChaosClothAsset/ConnectableValue.h"
#include "ChaosClothAsset/WeightedValue.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "ProxyDeformerNode.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

/** Selection Filter Set*/
USTRUCT()
struct FChaosClothAssetSelectionFilterSet
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableIStringValue RenderSelection = { TEXT("SelectionRenderFilterSet") };

	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableIStringValue SimSelection = { TEXT("SelectionSimFilterSet") };
};

/** Add the proxy deformer information to this cloth collection's render data. */
USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetProxyDeformerNode_v2 : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetProxyDeformerNode_v2, "ProxyDeformer", "Cloth", "Cloth Simulation Proxy Deformer")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FName("FClothCollection"), "Collection")

public:

	UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** Selection filter set used to restrict a selection of render vertices to a selection of simulation mesh triangles. Right click and do AddOptionPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, EditFixedSize, Category = "ProxyDeformer", Meta = (SkipInDisplayNameChain))
	TArray<FChaosClothAssetSelectionFilterSet> SelectionFilterSets;

	/** Whether using multiple simulation mesh triangles to influence the position of the deformed render vertex. */
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	bool bUseMultipleInfluences = false;

	/** The radius around the render vertices to look for all simulation mesh triangles influencing it (AKA SkinningKernelRadius). */
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer", Meta = (EditCondition = "bUseMultipleInfluences"))
	float InfluenceRadius = 5.f;

	FChaosClothAssetProxyDeformerNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:

	//~ Begin FDataflowNode interface
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
	virtual TArray<UE::Dataflow::FPin> AddPins() override;
	virtual bool CanAddPin() const override { return true; }
	virtual bool CanRemovePin() const override { return SelectionFilterSets.Num() > NumInitialSelectionFilterSets; }
	virtual TArray<UE::Dataflow::FPin> GetPinsToRemove() const override;
	virtual void OnPinRemoved(const UE::Dataflow::FPin& Pin) override;
	virtual void PostSerialize(const FArchive& Ar) override;
	//~ End FDataflowNode interface

	TArray<TPair<FName, FName>> GetSelectionFilterNames(UE::Dataflow::FContext& Context) const;
	UE::Dataflow::TConnectionReference<FString> GetRenderConnectionReference(int32 Index) const;
	UE::Dataflow::TConnectionReference<FString> GetSimConnectionReference(int32 Index) const;

	static constexpr int32 NumRequiredInputs = 1; // non-filter set inputs
	static constexpr int32 NumInitialSelectionFilterSets = 1;
};

/** Add the proxy deformer information to this cloth collection's render data. */
USTRUCT(Meta = (DataflowCloth, Deprecated = "5.5"))
struct UE_DEPRECATED(5.5, "Use the newer version of this node instead.") FChaosClothAssetProxyDeformerNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetProxyDeformerNode, "ProxyDeformer", "Cloth", "Cloth Simulation Proxy Deformer")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FName("FClothCollection"), "Collection")

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/**
	 * The name of a selection containing all the dynamic points. Must be of group type SimVertices2D, SimVertices3D, or SimFaces.
	 * Using an empty (or invalid) selection will make the proxy deformer consider all simulation points as dynamic points,
	 * and will fully contribute to the render mesh animations (as opposed to using the render mesh skinning for the non dynamic points).
	 * This selection is usually built from the same weight map set to the MaxDistance config using a WeightMapToSelection node and a very low threshold.
	 */
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableIStringValue SimVertexSelection;

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableIStringValue SelectionFilterSet0 = { TEXT("SelectionFilterSet") };  // Must be an IStringValue for the first element of the array

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet1 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet2 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet3 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet4 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet5 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet6 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet7 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet8 = { TEXT("SelectionFilterSet") };

	/** Selection filter set used to restrict a primary selection of render vertices to a secondary selection of simulation mesh triangles. Right click and do AddPin to add more selection sets .*/
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	FChaosClothAssetConnectableStringValue SelectionFilterSet9 = { TEXT("SelectionFilterSet") };

	/** Whether using multiple simulation mesh triangles to influence the position of the deformed render vertex. */
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	bool bUseMultipleInfluences = false;

	/** The radius around the render vertices to look for all simulation mesh triangles influencing it (AKA SkinningKernelRadius). */
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer", Meta = (EditCondition = "bUseMultipleInfluences"))
	float InfluenceRadius = 5.f;

	/**
	 * Whether to create a smoothed _SkinningBlendWeight render weight map to ease the transition between the deformed part and the skinned part of the render mesh.
	 * When no transition is created there will be a visible step in the rendered triangles around the edge of the kinematic/dynamic transition of the proxy simulation mesh.
	 * The _SkinningBlendWeight render weight map is created regardless of the transition being created smooth or not, and can be later adjusted using the weight map tool.
	 */
	UPROPERTY(EditAnywhere, Category = "ProxyDeformer")
	bool bUseSmoothTransition = true;

	/**
	 * The name of the render mesh weight map generated by this node detailing the contribution of the proxy deformer.
	 * Value ranges between 0 (fully deformed) and 1 (fully skinned).
	 * The name of this render mesh weight map cannot be changed and is only provided for further tweaking.
	 */
	UPROPERTY(VisibleAnywhere, Category = "ProxyDeformer", Meta = (DataflowOutput))
	FString SkinningBlendName;

	FChaosClothAssetProxyDeformerNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
	/** Hardcoded number of FilterSets since it is currently not possible to use arrays for optional inputs. */
	static constexpr int32 MaxNumFilterSets = 10;

	/** The number of filter sets currently exposed to the node UI. */
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UPROPERTY()
	int32 NumFilterSets = NumInitialOptionalInputs;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	//~ Begin FDataflowNode interface
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
	virtual TArray<UE::Dataflow::FPin> AddPins() override;
	virtual bool CanAddPin() const override { return NumFilterSets < MaxNumFilterSets; }
	virtual bool CanRemovePin() const override { return NumFilterSets > NumInitialOptionalInputs; }
	virtual TArray<UE::Dataflow::FPin> GetPinsToRemove() const override;
	virtual void OnPinRemoved(const UE::Dataflow::FPin& Pin) override;
	virtual void PostSerialize(const FArchive& Ar) override;
	//~ End FDataflowNode interface

	TArray<FName> GetSelectionFilterNames(UE::Dataflow::FContext& Context) const;
	TArray<const FChaosClothAssetConnectableStringValue*> Get1To9SelectionFilterSets() const;

	static constexpr int32 NumRequiredInputs = 2; // non-filter set inputs
	static constexpr int32 NumInitialOptionalInputs = 1; // filter set inputs that are created in the constructor.
};
