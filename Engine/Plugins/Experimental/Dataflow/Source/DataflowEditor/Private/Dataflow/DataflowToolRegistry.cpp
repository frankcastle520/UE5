// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowToolRegistry.h"
#include "Misc/LazySingleton.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Dataflow/DataflowEditorCommands.h"

namespace UE::Dataflow
{

	FDataflowToolRegistry& FDataflowToolRegistry::Get()
	{
		return TLazySingleton<FDataflowToolRegistry>::Get();
	}

	void FDataflowToolRegistry::TearDown()
	{
		TLazySingleton<FDataflowToolRegistry>::TearDown();
	}

	void FDataflowToolRegistry::AddNodeToToolMapping(const FName& NodeName, TObjectPtr<UInteractiveToolBuilder> ToolBuilder, const TSharedRef<const IDataflowToolActionCommands>& ToolActionRegistry)
	{
		ensureMsgf(!FDataflowEditorCommands::IsRegistered(), TEXT("FDataflowToolRegistry: DataflowEditorCommands have already been registered. \
				Newly registered Tools may not be available in the Editor. \
				Ensure that AddNodeToToolMapping is called before the DataflowEditor module is loaded."));

		// FUICommandInfo is uninitialized, it will be created later in FDataflowEditorCommandsImpl::RegisterCommands
		NodeTypeToToolMap.Add(NodeName, { ToolBuilder, ToolActionRegistry, nullptr });
	}

	void FDataflowToolRegistry::RemoveNodeToToolMapping(const FName& NodeName)
	{
		NodeTypeToToolMap.Remove(NodeName);
	}

	TArray<FName> FDataflowToolRegistry::GetNodeNames() const
	{
		TArray<FName> NodeNames;
		NodeTypeToToolMap.GetKeys(NodeNames);
		return NodeNames;
	}
	
	TSharedPtr<FUICommandInfo>& FDataflowToolRegistry::GetToolCommandForNode(const FName& NodeName)
	{
		check(NodeTypeToToolMap.Contains(NodeName));
		return NodeTypeToToolMap[NodeName].ToolCommand;
	}

	UInteractiveToolBuilder* FDataflowToolRegistry::GetToolBuilderForNode(const FName& NodeName)
	{
		check(NodeTypeToToolMap.Contains(NodeName));
		return NodeTypeToToolMap[NodeName].ToolBuilder;
	}

	const UInteractiveToolBuilder* FDataflowToolRegistry::GetToolBuilderForNode(const FName& NodeName) const
	{
		check(NodeTypeToToolMap.Contains(NodeName));
		return NodeTypeToToolMap[NodeName].ToolBuilder;
	}

	void FDataflowToolRegistry::UnbindActiveCommands(const TSharedPtr<FUICommandList>& UICommandList) const
	{
		for (const TPair<FName, FToolInfo>& Entry : NodeTypeToToolMap)
		{
			Entry.Value.ToolActionCommands->UnbindActiveCommands(UICommandList);
		}
	}

	void FDataflowToolRegistry::BindCommandsForCurrentTool(const TSharedPtr<FUICommandList>& UICommandList, UInteractiveTool* Tool) const
	{
		for (const TPair<FName, FToolInfo>& Entry : NodeTypeToToolMap)
		{
			Entry.Value.ToolActionCommands->BindCommandsForCurrentTool(UICommandList, Tool);
		}
	}

}
