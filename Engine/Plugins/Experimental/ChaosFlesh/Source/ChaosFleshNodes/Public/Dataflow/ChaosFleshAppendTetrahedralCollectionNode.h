// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Dataflow/DataflowSelection.h"

#include "ChaosFleshAppendTetrahedralCollectionNode.generated.h"

/**
 * Append another Tetrahedral Collection to this collection. All attributes will be copied.
 */
USTRUCT(meta = (DataflowFlesh))
struct FAppendTetrahedralCollectionDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FAppendTetrahedralCollectionDataflowNode, "AppendTetrahedralCollection", "Flesh", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FGeometryCollection::StaticType(), "Collection1")
public:
	UPROPERTY(meta = (DataflowInput, DataflowOutput, DisplayName = "Collection1", DataflowPassthrough = "Collection1"));
	FManagedArrayCollection Collection1;

	UPROPERTY(meta = (DataflowInput, DisplayName = "Collection2"));
	FManagedArrayCollection Collection2;

	/* if transforms from Collection2 are merged into Collection1 by matching transform name */
	UPROPERTY(EditAnywhere, Category = "Dataflow", meta = (DisplayName = "Merge Transform"));
	bool bMergeTransform = false;

	UPROPERTY(meta = (DataflowOutput))
	FDataflowGeometrySelection GeometrySelection1;

	UPROPERTY(meta = (DataflowOutput))
	FDataflowGeometrySelection GeometrySelection2;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "GeometryGroupGuids1"))
	TArray<FString> GeometryGroupGuidsOut1;

	UPROPERTY(meta = (DataflowOutput, DisplayName = "GeometryGroupGuids2"))
	TArray<FString> GeometryGroupGuidsOut2;

	FAppendTetrahedralCollectionDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid())
		: FDataflowNode(InParam, InGuid)
	{
		RegisterInputConnection(&Collection1);
		RegisterInputConnection(&Collection2);
		RegisterOutputConnection(&Collection1, &Collection1);
		RegisterOutputConnection(&GeometryGroupGuidsOut1);
		RegisterOutputConnection(&GeometryGroupGuidsOut2);
		RegisterOutputConnection(&GeometrySelection1);
		RegisterOutputConnection(&GeometrySelection2);
	}

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};