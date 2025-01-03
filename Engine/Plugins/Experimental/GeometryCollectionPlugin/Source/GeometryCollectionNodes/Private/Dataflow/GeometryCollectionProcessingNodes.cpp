// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionProcessingNodes.h"

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"


namespace UE::Dataflow
{
	void GeometryCollectionProcessingNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCloseGeometryOnCollectionDataflowNode);
	}
}

void FCloseGeometryOnCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<DataType>(&Collection))
	{
		DataType InCollection = GetValue<DataType>(Context, &Collection);

		//
		// @todo(dataflow) : Implemention that closes the geometry on the collection
		//

		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}





