// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEngine.h"

#include "ChaosFleshComputeMuscleActivationNode.generated.h"

/**
* Computes an orthogonal matrix for each element
* M = [v,w,u], where v is the fiber direction of that element, w and u are chosen to be orthogonal to v and each other.
*/
USTRUCT(meta = (DataflowFlesh))
struct FComputeMuscleActivationDataNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FComputeMuscleActivationDataNode, "ComputeMuscleActivationData", "Flesh", "")

public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection", DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	UPROPERTY(meta = (DataflowInput, DisplayName = "OriginVertexIndices"))
		TArray<int32> OriginIndicesIn;

	UPROPERTY(meta = (DataflowInput, DisplayName = "InsertionVertexIndices"))
		TArray<int32> InsertionIndicesIn;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
		float ContractionVolumeScale = 1.f;

	FComputeMuscleActivationDataNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection);
		RegisterInputConnection(&OriginIndicesIn);
		RegisterInputConnection(&InsertionIndicesIn);
		RegisterOutputConnection(&Collection, &Collection);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
