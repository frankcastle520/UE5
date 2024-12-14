// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowStaticMeshNodes.h"
#include "Engine/StaticMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowStaticMeshNodes)

namespace UE::Dataflow
{
	void RegisterStaticMeshNodes()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetStaticMeshDataflowNode);
	}
}

void FGetStaticMeshDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	typedef TObjectPtr<const UStaticMesh> DataType;
	if (Out->IsA<DataType>(&StaticMesh))
	{
		SetValue(Context, StaticMesh, &StaticMesh); // prime to avoid ensure

		if (StaticMesh)
		{
			SetValue(Context, StaticMesh, &StaticMesh);
		}
		else if (const UE::Dataflow::FEngineContext* EngineContext = Context.AsType<UE::Dataflow::FEngineContext>())
		{
			if (const UStaticMesh* StaticMeshFromOwner = UE::Dataflow::Reflection::FindObjectPtrProperty<UStaticMesh>(
				EngineContext->Owner, PropertyName))
			{
				SetValue(Context, DataType(StaticMeshFromOwner), &StaticMesh);

			}
		}
	}
}


