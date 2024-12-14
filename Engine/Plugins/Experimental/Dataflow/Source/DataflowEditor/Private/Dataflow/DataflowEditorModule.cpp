// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorModule.h"

#include "Dataflow/DataflowEditorStyle.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEngineRendering.h"
#include "Dataflow/DataflowFunctionProperty.h"
#include "Dataflow/DataflowFunctionPropertyCustomization.h"
#include "Dataflow/DataflowSNodeFactories.h"
#include "Dataflow/ScalarVertexPropertyGroupCustomization.h"
#include "Dataflow/DataflowToolRegistry.h"
#include "DataflowEditorTools/DataflowEditorWeightMapPaintTool.h"
#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"

#include "PropertyEditorModule.h"
#include "EditorModeRegistry.h"

#define LOCTEXT_NAMESPACE "DataflowEditor"

const FColor FDataflowEditorModule::SurfaceColor = FLinearColor(0.6, 0.6, 0.6).ToRGBE();
static const FName ScalarVertexPropertyGroupName = TEXT("ScalarVertexPropertyGroup");
static const FName DataflowFunctionPropertyName = TEXT("DataflowFunctionProperty");

class FDataflowEditorWeightMapPaintToolActionCommands : public TInteractiveToolCommands<FDataflowEditorWeightMapPaintToolActionCommands>
{
public:
	FDataflowEditorWeightMapPaintToolActionCommands() : 
		TInteractiveToolCommands<FDataflowEditorWeightMapPaintToolActionCommands>(
			TEXT("DataflowEditorWeightMapPaintToolContext"),
			LOCTEXT("DataflowEditorWeightMapPaintToolContext", "Dataflow Weight Map Paint Tool Context"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override
	{
		ToolCDOs.Add(GetMutableDefault<UDataflowEditorWeightMapPaintTool>());
	}
};


class FDataflowToolActionCommandBindings : public UE::Dataflow::FDataflowToolRegistry::IDataflowToolActionCommands
{
public:
	FDataflowToolActionCommandBindings()
	{
		FDataflowEditorWeightMapPaintToolActionCommands::Register();
	}

	virtual void UnbindActiveCommands(const TSharedPtr<FUICommandList>& UICommandList) const override
	{
		checkf(FDataflowEditorWeightMapPaintToolActionCommands::IsRegistered(), TEXT("Expected WeightMapPaintTool actions to have been registered"));
		FDataflowEditorWeightMapPaintToolActionCommands::Get().UnbindActiveCommands(UICommandList);
	}

	virtual void BindCommandsForCurrentTool(const TSharedPtr<FUICommandList>& UICommandList, UInteractiveTool* Tool) const override
	{
		if (ExactCast<UDataflowEditorWeightMapPaintTool>(Tool))
		{
			checkf(FDataflowEditorWeightMapPaintToolActionCommands::IsRegistered(), TEXT("Expected WeightMapPaintTool actions to have been registered"));
			FDataflowEditorWeightMapPaintToolActionCommands::Get().BindCommandsForCurrentTool(UICommandList, Tool);
		}
	}
};


void FDataflowEditorModule::StartupModule()
{
	FDataflowEditorStyle::Get();
	
	// Register type customizations
	if (FPropertyEditorModule* const PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->RegisterCustomPropertyTypeLayout(ScalarVertexPropertyGroupName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::Dataflow::FScalarVertexPropertyGroupCustomization::MakeInstance));
		PropertyModule->RegisterCustomPropertyTypeLayout(DataflowFunctionPropertyName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::Dataflow::FFunctionPropertyCustomization::MakeInstance));
	}

	UE::Dataflow::RenderingCallbacks();

	UE::Dataflow::FDataflowToolRegistry& ToolRegistry = UE::Dataflow::FDataflowToolRegistry::Get();

	UDataflowEditorWeightMapPaintToolBuilder* const ToolBuilder = NewObject<UDataflowEditorWeightMapPaintToolBuilder>();
	TSharedRef<const FDataflowToolActionCommandBindings> Actions = MakeShared<FDataflowToolActionCommandBindings>();
	ToolRegistry.AddNodeToToolMapping(FDataflowCollectionAddScalarVertexPropertyNode::StaticType(), ToolBuilder, Actions);

	FDataflowEditorCommands::Register();
}

void FDataflowEditorModule::ShutdownModule()
{	
	FEditorModeRegistry::Get().UnregisterMode(UDataflowEditorMode::EM_DataflowEditorModeId);

	// Deregister type customizations
	if (FPropertyEditorModule* const PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout(ScalarVertexPropertyGroupName);
		PropertyModule->UnregisterCustomPropertyTypeLayout(DataflowFunctionPropertyName);
	}

	FDataflowEditorCommands::Unregister();

	UE::Dataflow::FDataflowToolRegistry& ToolRegistry = UE::Dataflow::FDataflowToolRegistry::Get();
	ToolRegistry.RemoveNodeToToolMapping(FDataflowCollectionAddScalarVertexPropertyNode::StaticType());
}

IMPLEMENT_MODULE(FDataflowEditorModule, DataflowEditor)


#undef LOCTEXT_NAMESPACE
