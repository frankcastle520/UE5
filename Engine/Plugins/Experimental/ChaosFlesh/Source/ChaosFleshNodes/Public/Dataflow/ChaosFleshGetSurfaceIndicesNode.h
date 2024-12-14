// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSelection.h"
#include "ChaosFleshGetSurfaceIndicesNode.generated.h"

USTRUCT(meta = (DataflowFlesh))
struct FGetSurfaceIndicesNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FGetSurfaceIndicesNode, "GetSurfaceIndices", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FGeometryCollection::StaticType(), "Collection")

public:
	UPROPERTY(meta = (DataflowInput, DisplayName = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DisplayName = "GeometryGroupGuidsIn"))
	TArray<FString> GeometryGroupGuidsIn;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "SurfaceVertexSelection"))
	FDataflowVertexSelection SurfaceVertexSelection;

	FGetSurfaceIndicesNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&GeometryGroupGuidsIn);
		RegisterOutputConnection(&SurfaceVertexSelection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};

namespace UE::Dataflow
{
	void RegisterChaosFleshEngineAssetNodes();
}

