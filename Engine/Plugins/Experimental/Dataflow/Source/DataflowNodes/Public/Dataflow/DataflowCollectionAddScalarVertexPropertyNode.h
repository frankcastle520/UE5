// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Dataflow/DataflowNode.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Dataflow/DataflowCollectionAttributeKeyNodes.h"

#include "DataflowCollectionAddScalarVertexPropertyNode.generated.h"


class IDataflowAddScalarVertexPropertyCallbacks
{
public:
	virtual ~IDataflowAddScalarVertexPropertyCallbacks() = default;
	virtual FName GetName() const = 0;
	virtual TArray<FName> GetTargetGroupNames() const = 0;
	virtual TArray<UE::Dataflow::FRenderingParameter> GetRenderingParameters() const = 0;
};

class DataflowAddScalarVertexPropertyCallbackRegistry
{
public:

	DATAFLOWNODES_API static DataflowAddScalarVertexPropertyCallbackRegistry& Get();
	DATAFLOWNODES_API static void TearDown();

	DATAFLOWNODES_API void RegisterCallbacks(TUniquePtr<IDataflowAddScalarVertexPropertyCallbacks>&& Callbacks);

	DATAFLOWNODES_API void DeregisterCallbacks(const FName& CallbacksName);

	DATAFLOWNODES_API TArray<FName> GetTargetGroupNames() const;

	DATAFLOWNODES_API TArray<UE::Dataflow::FRenderingParameter> GetRenderingParameters() const;

private:

	TMap<FName, TUniquePtr<IDataflowAddScalarVertexPropertyCallbacks>> AllCallbacks;
};


/*
* Custom type so that we can use property type customization
*/
USTRUCT()
struct FScalarVertexPropertyGroup
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Vertex Group")
	FName Name = FGeometryCollection::VerticesGroup;
};


/** Scalar vertex properties. */
USTRUCT(Meta = (DataflowCollection))
struct DATAFLOWNODES_API FDataflowCollectionAddScalarVertexPropertyNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FDataflowCollectionAddScalarVertexPropertyNode, "AddScalarVertexProperty", "Collection", "Add a saved scalar property to a collection")

	virtual TArray<UE::Dataflow::FRenderingParameter> GetRenderParametersImpl() const override;

public:

	UPROPERTY(Meta = (DataflowInput, DataflowOutput, DataflowPassthrough = "Collection"))
	FManagedArrayCollection Collection;

	/** The name to be set as a weight map attribute. */
	UPROPERTY(EditAnywhere, Category = "Vertex Attribute")
	FString Name;

	UPROPERTY(meta = (DisplayName = "AttributeKey", DataflowOutput))
	FCollectionAttributeKey AttributeKey;

	UPROPERTY()
	TArray<float> VertexWeights;

	UPROPERTY(EditAnywhere, Category = "Vertex Attribute")
	FScalarVertexPropertyGroup TargetGroup;

	FDataflowCollectionAddScalarVertexPropertyNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
};
