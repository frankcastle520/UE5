// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "CopySimulationToRenderMeshNode.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

class UMaterialInterface;

/** Copy the simulation mesh to the render mesh to be able to render the simulation mesh, or when not using a different mesh for rendering. */
USTRUCT(Meta = (DataflowCloth))
struct FChaosClothAssetCopySimulationToRenderMeshNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetCopySimulationToRenderMeshNode, "CopySimulationToRenderMesh", "Cloth", "Cloth Simulation Render Mesh")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FName("FClothCollection"), "Collection")

public:

	UPROPERTY(Meta = (Dataflowinput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** New material for the render mesh. */
	UPROPERTY(EditAnywhere, Category = "Copy Simulation To Render Mesh")
	TObjectPtr<const UMaterialInterface> Material;

	/** Generate a single render pattern rather than a render pattern per sim pattern. */
	UPROPERTY(EditAnywhere, Category = "Copy Simulation To Render Mesh")
	bool bGenerateSingleRenderPattern = true;

	FChaosClothAssetCopySimulationToRenderMeshNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
