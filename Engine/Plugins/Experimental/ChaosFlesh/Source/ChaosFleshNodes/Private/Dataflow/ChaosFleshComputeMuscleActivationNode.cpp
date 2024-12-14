// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/ChaosFleshComputeMuscleActivationNode.h"

#include "GeometryCollection/Facades/CollectionMeshFacade.h"
#include "GeometryCollection/Facades/CollectionMuscleActivationFacade.h"
#include "ChaosFlesh/TetrahedralCollection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosFleshComputeMuscleActivationNode)

void FComputeMuscleActivationDataNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		TArray<int32> InOriginIndices = GetValue<TArray<int32>>(Context, &OriginIndicesIn);
		TArray<int32> InInsertionIndices = GetValue<TArray<int32>>(Context, &InsertionIndicesIn);
		
		GeometryCollection::Facades::FMuscleActivationFacade FMuscleActivation(InCollection);
		FMuscleActivation.SetUpMuscleActivation(InOriginIndices, InInsertionIndices, ContractionVolumeScale);
		Out->SetValue(MoveTemp(InCollection), Context);
	}
}
