// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNodesPlugin.h"

#include "CoreMinimal.h"
#include "Dataflow/DataflowCollectionAttributeKeyNodes.h"
#include "Dataflow/DataflowSkeletalMeshNodes.h"
#include "Dataflow/DataflowStaticMeshNodes.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowSelectionNodes.h"
#include "Dataflow/DataflowContextOverridesNodes.h"
#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"

#define LOCTEXT_NAMESPACE "DataflowNodes"

class FGeometryCollectionAddScalarVertexPropertyCallbacks : public IDataflowAddScalarVertexPropertyCallbacks
{
public:

	const static FName Name;

	virtual ~FGeometryCollectionAddScalarVertexPropertyCallbacks() = default;

	virtual FName GetName() const override
	{
		return Name;
	}

	virtual TArray<FName> GetTargetGroupNames() const override
	{
		return { FGeometryCollection::VerticesGroup };
	}

	virtual TArray<UE::Dataflow::FRenderingParameter> GetRenderingParameters() const override
	{
		return { { TEXT("SurfaceRender"), FGeometryCollection::StaticType(), {TEXT("Collection")} } };
	}
};

const FName FGeometryCollectionAddScalarVertexPropertyCallbacks::Name = FName("FGeometryCollectionAddScalarVertexPropertyCallbacks");

void IDataflowNodesPlugin::StartupModule()
{
	UE::Dataflow::RegisterSkeletalMeshNodes();
	UE::Dataflow::RegisterStaticMeshNodes();
	UE::Dataflow::RegisterSelectionNodes();
	UE::Dataflow::RegisterContextOverridesNodes();
	UE::Dataflow::DataflowCollectionAttributeKeyNodes();
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowCollectionAddScalarVertexPropertyNode);

	UE::Dataflow::RegisterNodeFilter(FDataflowTerminalNode::StaticType());

	DataflowAddScalarVertexPropertyCallbackRegistry::Get().RegisterCallbacks(MakeUnique<FGeometryCollectionAddScalarVertexPropertyCallbacks>());
}

void IDataflowNodesPlugin::ShutdownModule()
{
	DataflowAddScalarVertexPropertyCallbackRegistry::Get().DeregisterCallbacks(FGeometryCollectionAddScalarVertexPropertyCallbacks::Name);
}


IMPLEMENT_MODULE(IDataflowNodesPlugin, DataflowNodes)


#undef LOCTEXT_NAMESPACE
