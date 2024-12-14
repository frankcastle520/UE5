// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorMode.h"

#include "AssetEditorModeManager.h"
#include "AttributeEditorTool.h"
#include "BaseGizmos/TransformGizmoUtil.h"
#include "Components/DynamicMeshComponent.h"
#include "ContextObjectStore.h"
#include "DataflowEditorTools/DataflowEditorWeightMapPaintTool.h"
#include "Dataflow/DataflowComponentToolTarget.h"
#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"
#include "Dataflow/DataflowConstructionViewportClient.h"
#include "Dataflow/DataflowEditor.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEditorModeToolkit.h"
#include "Dataflow/DataflowEditorUtil.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowEditorPreviewSceneBase.h"
#include "Dataflow/DataflowConstructionScene.h"
#include "Dataflow/DataflowRenderingViewMode.h"
#include "Dataflow/DataflowSimulationScene.h"
#include "Dataflow/DataflowSimulationViewportClient.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowToolTarget.h"
#include "Dataflow/DataflowToolRegistry.h"
#include "EditorModeManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "EngineAnalytics.h"
#include "MeshSelectionTool.h"
#include "MeshVertexPaintTool.h"
#include "MeshAttributePaintTool.h"
#include "ModelingToolTargetUtil.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "ToolTargetManager.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"
#include "ToolTargets/StaticMeshToolTarget.h"
#include "ToolTargets/SkeletalMeshComponentToolTarget.h"
#include "ToolTargets/SkeletalMeshToolTarget.h"
#include "Tools/UEdMode.h"
#include "Selection.h"
#include "UnrealClient.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowEditorMode)

#define LOCTEXT_NAMESPACE "UDataflowEditorMode"

const FEditorModeID UDataflowEditorMode::EM_DataflowEditorModeId = TEXT("EM_DataflowAssetEditorMode");

namespace UE::Dataflow::Private
{
	bool bDataflowEditorEnableToolsInPIE = true;
	FAutoConsoleVariableRef CVARDataflowEditorEnableToolsInPIE(TEXT("p.Dataflow.EnableToolsInPIE"), bDataflowEditorEnableToolsInPIE,
		TEXT("Enable Dataflow Editor tools while Play In Editor is running [def:true]"));
}

UDataflowEditorMode::UDataflowEditorMode()
{
	Info = FEditorModeInfo(
		EM_DataflowEditorModeId,
		LOCTEXT("DataflowEditorModeName", "Dataflow"),
		FSlateIcon(),
		false);
}

const FToolTargetTypeRequirements& UDataflowEditorMode::GetToolTargetRequirements()
{
	static const FToolTargetTypeRequirements ToolTargetRequirements =
		FToolTargetTypeRequirements({
			UMaterialProvider::StaticClass(),
			UDynamicMeshCommitter::StaticClass(),
			UDynamicMeshProvider::StaticClass()
			});

	return ToolTargetRequirements;
}

void UDataflowEditorMode::Enter()
{
	UBaseCharacterFXEditorMode::Enter();

	// Register gizmo ContextObject for use inside interactive tools
	UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(GetInteractiveToolsContext());

	// Initialize view mode to a default
	ConstructionViewMode = UE::Dataflow::FRenderingViewModeFactory::GetInstance().GetViewMode(UE::Dataflow::FDataflowConstruction3DViewMode::Name);

	// Log mode starting
	if (FEngineAnalytics::IsAvailable())
	{
		LastModeStartTimestamp = FDateTime::UtcNow();
		TArray<FAnalyticsEventAttribute> EventAttributes;
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), LastModeStartTimestamp.ToString()));
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.DataflowEditor.Enter"), EventAttributes);
	}

}

void UDataflowEditorMode::SetDataflowEditor(UDataflowEditor* InDataflowEditor) 
{ 
	DataflowEditor = InDataflowEditor; 
}

void UDataflowEditorMode::AddToolTargetFactories()
{
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UStaticMeshToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<USkeletalMeshComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<USkeletalMeshToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UDataflowComponentToolTargetFactory>(GetToolManager()));
	GetInteractiveToolsContext()->TargetManager->AddTargetFactory(NewObject<UDataflowToolTargetFactory>(GetToolManager()));
}

void UDataflowEditorMode::RegisterDataflowTool(TSharedPtr<FUICommandInfo> UICommand,
	FString ToolIdentifier,
	UInteractiveToolBuilder* Builder,
	UEditorInteractiveToolsContext* const ToolsContext,
	EToolsContextScope ToolScope)
{
	if (!Toolkit.IsValid())
	{
		return;
	}

	if (!ToolsContext)
	{
		return;
	}

	if (ToolScope == EToolsContextScope::Default)
	{
		ToolScope = GetDefaultToolScope();
	}
	ensure(ToolScope != EToolsContextScope::Editor);

	ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);

	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	CommandList->MapAction(UICommand, 
		FExecuteAction::CreateWeakLambda(ToolsContext, [this, ToolsContext, ToolIdentifier, Builder]()
		{
			UDataflowContextObject* ContextObject = ToolsContext->ContextObjectStore->FindContext<UDataflowContextObject>();
			check(ContextObject);

			if (const IDataflowEditorToolBuilder* const DataflowToolBuilder = Cast<IDataflowEditorToolBuilder>(Builder))
			{
				// Check if we need to switch view modes before starting the tool
				TArray<const UE::Dataflow::IDataflowConstructionViewMode*> SupportedModes;

				DataflowToolBuilder->GetSupportedConstructionViewModes(*ContextObject, SupportedModes);

				if (SupportedModes.Num() > 0 && !SupportedModes.Contains(GetConstructionViewMode()))
				{
					if (!bShouldRestoreSavedConstructionViewMode)
					{
						// remember the current view mode so we can restore it later
						SavedConstructionViewMode = GetConstructionViewMode()->GetName();
						bShouldRestoreSavedConstructionViewMode = true;
					}

					bool bHadSingleSelectedMesh = false;
					if (const USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents())
					{
						bHadSingleSelectedMesh = (SelectedComponents->Num() == 1);
					}

					// switch to the preferred view mode for the tool that's about to start
					SetConstructionViewMode(SupportedModes[0]->GetName());

					if (bHadSingleSelectedMesh)
					{
						// If there is a single dynamic mesh component in the scene, select it so the tool can start
						const TArray<UDynamicMeshComponent*> DynamicMeshComponents = ConstructionScene->GetDynamicMeshComponents();
						if (DynamicMeshComponents.Num() == 1)
						{
							if (USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents())
							{
								SelectedComponents->Select(DynamicMeshComponents[0]);
							}
						}
					}

				}
			}

			// Make sure the ContextObject's selected Collection is the from the Input side of the selected node (so that the tool gets the Collection as it appears before node execution)

			if (TSharedPtr<UE::Dataflow::FEngineContext> DataflowContext = ContextObject->GetDataflowContext())
			{
				if (UDataflowEdNode* const SelectedNode = ContextObject->GetSelectedNode())
				{
					if (const TSharedPtr<FDataflowNode> DataflowNode = SelectedNode->GetDataflowNode())
					{
						for (const FDataflowInput* const Input : DataflowNode->GetInputs())
						{
							if (Input->GetType() == FName("FManagedArrayCollection"))
							{
								const FManagedArrayCollection DefaultValue;
								TSharedRef<FManagedArrayCollection> Collection = MakeShared<FManagedArrayCollection>(Input->GetValue<FManagedArrayCollection>(*DataflowContext, DefaultValue));

								constexpr bool bCollectionIsInput = true;
								ContextObject->SetSelectedCollection(Collection, bCollectionIsInput);

								// If we have multiple input Collections, this will just take the first one. This is what the Cloth Editor does as well (but there it also checks to see if it's a ClothCollection)
								break;
							}
						}
					}
				}
			}

			ActiveToolsContext = ToolsContext;
			ToolsContext->StartTool(ToolIdentifier);
		}),
		FCanExecuteAction::CreateWeakLambda(ToolsContext, [this, ToolIdentifier, ToolsContext]()
		{
			return ShouldToolStartBeAllowed(ToolIdentifier) &&
			ToolsContext->ToolManager->CanActivateTool(EToolSide::Mouse, ToolIdentifier);
		}),
		FIsActionChecked::CreateUObject(ToolsContext, &UEdModeInteractiveToolsContext::IsToolActive, EToolSide::Mouse, ToolIdentifier),
		EUIActionRepeatMode::RepeatDisabled
	);
}

void UDataflowEditorMode::AddNode(FName NewNodeType)
{
	const FName ConnectionType = FManagedArrayCollection::StaticType();
	const FName ConnectionName("Collection");

	UEdGraphNode* const CurrentlySelectedNode = GetSingleSelectedNodeWithOutputType(ConnectionType);
	checkf(CurrentlySelectedNode, TEXT("No node with FManagedArrayCollection output is currently selected in the Dataflow graph"));

	const UEdGraphNode* const NewNode = CreateAndConnectNewNode(NewNodeType, *CurrentlySelectedNode, ConnectionType, ConnectionName);
	verifyf(NewNode, TEXT("Failed to create a new node: %s"), *NewNodeType.ToString());

	StartToolForSelectedNode(NewNode);
}

bool UDataflowEditorMode::CanAddNode(FName NewNodeType) const
{
	const UEdGraphNode* const CurrentlySelectedNode = GetSingleSelectedNodeWithOutputType(FManagedArrayCollection::StaticType());
	return (CurrentlySelectedNode != nullptr);
}

void UDataflowEditorMode::RegisterTools()
{
	const FDataflowEditorCommandsImpl& CommandInfos = FDataflowEditorCommands::Get();

	UEditorInteractiveToolsContext* const ConstructionViewportToolsContext = GetInteractiveToolsContext();

	UE::Dataflow::FDataflowToolRegistry& ToolRegistry = UE::Dataflow::FDataflowToolRegistry::Get();
	const TArray<FName> NodeNames = ToolRegistry.GetNodeNames();
	for (const FName& RegisteredNodeName : NodeNames)
	{
		const TSharedPtr<FUICommandInfo> CommandInfo = ToolRegistry.GetToolCommandForNode(RegisteredNodeName);
		UInteractiveToolBuilder* const Builder = ToolRegistry.GetToolBuilderForNode(RegisteredNodeName);

		// TODO: This is here only so the Tool can hide the all meshes in the DataflowConstructionScene. That should probably be handed in this class instead.
		if (UDataflowEditorWeightMapPaintToolBuilder* WeightMapPaintToolBuilder = Cast<UDataflowEditorWeightMapPaintToolBuilder>(Builder))
		{
			WeightMapPaintToolBuilder->SetEditorMode(this);
		}

		RegisterDataflowTool(CommandInfo, RegisteredNodeName.ToString() + FString(TEXT("Tool")), Builder, ConstructionViewportToolsContext);

		NodeTypeToToolCommandMap.Add(RegisteredNodeName, CommandInfo);
	}

	// Register "Add Node" commands for buttons in the UI. The EditorToolkit will construct the actual toolbar buttons.
	NodeTypeToAddNodeCommandMap.Add(FDataflowCollectionAddScalarVertexPropertyNode::StaticType(), CommandInfos.AddWeightMapNode);
}

bool UDataflowEditorMode::ShouldToolStartBeAllowed(const FString& ToolIdentifier) const
{
	// Allow switching away from tool if no changes have been made in the tool yet (which we infer from the CanAccept status)
	if (GetInteractiveToolsContext()->CanAcceptActiveTool())
	{
		return false;
	}

	if (ConstructionScene && ConstructionScene->GetDataflowModeManager() && ConstructionScene->GetDataflowModeManager()->GetInteractiveToolsContext())
	{
		if (ConstructionScene->GetDataflowModeManager()->GetInteractiveToolsContext()->HasActiveTool())
		{
			return false;
		}
	}


	if (UE::Dataflow::Private::bDataflowEditorEnableToolsInPIE)
	{
		// UEdMode::ShouldToolStartBeAllowed returns (!GEditor->PlayWorld && !GIsPlayInEditorWorld) but we want to allow tools to start while in PIE
		return true;
	}
	else
	{
		return UBaseCharacterFXEditorMode::ShouldToolStartBeAllowed(ToolIdentifier);
	}
}

void UDataflowEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FDataflowEditorModeToolkit>();
}


void UDataflowEditorMode::SetWireframeRenderToggleEnabled(bool bEnable)
{
	if (const TObjectPtr<UDataflowBaseContent> EditorContent = ConstructionScene->GetEditorContent())
	{
		if (const TObjectPtr<UDataflow> DataflowGraph = EditorContent->GetDataflowAsset())
		{
			for (UEdGraphNode* const EdGraphNode : DataflowGraph->Nodes)
			{
				if (UDataflowEdNode* const DataflowEdNode = Cast<UDataflowEdNode>(EdGraphNode))
				{
					DataflowEdNode->SetCanEnableWireframeRenderNode(bEnable);
				}
			}
		}
	}
}

void UDataflowEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FDataflowEditorCommandsImpl::UpdateToolCommandBinding(Tool, ToolCommandList, false);

	// Temporarily disable wireframe render toggle switch on all nodes
	SetWireframeRenderToggleEnabled(false);
}

void UDataflowEditorMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FDataflowEditorCommandsImpl::UpdateToolCommandBinding(Tool, ToolCommandList, true);

	if (bShouldRestoreConstructionViewWireframe)
	{
		bConstructionViewWireframe = true;
		bShouldRestoreConstructionViewWireframe = false;
	}

	if (bShouldRestoreSavedConstructionViewMode)
	{
		SetConstructionViewMode(SavedConstructionViewMode);
		bShouldRestoreSavedConstructionViewMode = false;
	}
	else
	{
		ConstructionScene->ResetConstructionScene();
	}

	if (TSharedPtr<SDataflowGraphEditor> GraphEditor = DataflowGraphEditor.Pin())
	{
		GraphEditor->SetEnabled(true);
	}

	// Re-enable wireframe render toggle switch on all nodes
	SetWireframeRenderToggleEnabled(true);
}

void UDataflowEditorMode::BindCommands()
{
	const FDataflowEditorCommandsImpl& CommandInfos = FDataflowEditorCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	// Hook up to Enter/Esc key presses
	CommandList->MapAction(
		CommandInfos.AcceptOrCompleteActiveTool,
		FExecuteAction::CreateLambda([this](){ AcceptActiveToolActionOrTool();}),
		FCanExecuteAction::CreateLambda([this]() {
				return GetInteractiveToolsContext()->CanAcceptActiveTool() || GetInteractiveToolsContext()->CanCompleteActiveTool();
			}),
		FGetActionCheckState(),
		FIsActionButtonVisible(),
		EUIActionRepeatMode::RepeatDisabled);

	CommandList->MapAction(
		CommandInfos.CancelOrCompleteActiveTool,
		FExecuteAction::CreateLambda([this]() { CancelActiveToolActionOrTool(); }),
		FCanExecuteAction::CreateLambda([this]() {
			return GetInteractiveToolsContext()->CanCompleteActiveTool() || GetInteractiveToolsContext()->CanCancelActiveTool();
			}),
		FGetActionCheckState(),
		FIsActionButtonVisible(),
		EUIActionRepeatMode::RepeatDisabled);
}

void UDataflowEditorMode::Exit()
{
	UActorComponent::MarkRenderStateDirtyEvent.RemoveAll(this);

	if(ConstructionScene)
	{
		ConstructionScene->ResetConstructionScene();
		ConstructionScene = nullptr;
	}

	if(SimulationScene)
	{
		SimulationScene->ResetSimulationScene();
		SimulationScene = nullptr;
	}

	// Log mode exit
	if (FEngineAnalytics::IsAvailable())
	{
		const FTimespan ModeUsageDuration = FDateTime::UtcNow() - LastModeStartTimestamp;

		TArray<FAnalyticsEventAttribute> Attributes;
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), FDateTime::UtcNow().ToString()));
		Attributes.Add(FAnalyticsEventAttribute(TEXT("Duration.Seconds"), static_cast<float>(ModeUsageDuration.GetTotalSeconds())));

		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.DataflowEditor.Exit"));
	}

	Super::Exit();
}

void UDataflowEditorMode::SetDataflowConstructionScene(FDataflowConstructionScene* InConstructionScene)
{
	ConstructionScene = InConstructionScene;

	UEditorInteractiveToolsContext* const PreviewToolsContext = ConstructionScene->GetDataflowModeManager()->GetInteractiveToolsContext();
	UInteractiveToolManager* const PreviewToolManager = PreviewToolsContext->ToolManager;
	//#todo(brice): Make sure AddToolTargetFactories has been called. 
	//PreviewToolsContext->TargetManager->AddTargetFactory(NewObject<UClothComponentToolTargetFactory>(PreviewToolManager));
	//PreviewToolsContext->TargetManager->AddTargetFactory(NewObject<USkeletalMeshComponentToolTargetFactory>(PreviewToolManager));

	PreviewToolManager->OnToolStarted.AddUObject(this, &UDataflowEditorMode::OnToolStarted);
	PreviewToolManager->OnToolEnded.AddUObject(this, &UDataflowEditorMode::OnToolEnded);

	check(Toolkit.IsValid());

	// FBaseToolkit's OnToolStarted and OnToolEnded are protected, so we use the subclass to get at them
	FDataflowEditorModeToolkit* const DataflowModeToolkit = static_cast<FDataflowEditorModeToolkit*>(Toolkit.Get());

	PreviewToolManager->OnToolStarted.AddSP(DataflowModeToolkit, &FDataflowEditorModeToolkit::OnToolStarted);
	PreviewToolManager->OnToolEnded.AddSP(DataflowModeToolkit, &FDataflowEditorModeToolkit::OnToolEnded);
}

void UDataflowEditorMode::SetDataflowSimlationScene(FDataflowSimulationScene* InSimulationScene)
{
	SimulationScene = InSimulationScene;
}

void UDataflowEditorMode::CreateToolTargets(const TArray<TObjectPtr<UObject>>& AssetsIn)
{
	ToolTargets.Reset();
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = ConstructionScene->GetEditorContent())
	{
		if (UToolTarget* Target = GetInteractiveToolsContext()->TargetManager->BuildTarget(EditorContent, GetToolTargetRequirements()))
		{
			ToolTargets.Add(Target);
		}
	}
}

bool UDataflowEditorMode::IsComponentSelected(const UPrimitiveComponent* InComponent)
{
	if (const FEditorModeTools* const ModeManager = GetModeManager())
	{
		if (const UTypedElementSelectionSet* const TypedElementSelectionSet = ModeManager->GetEditorSelectionSet())
		{
			if (const FTypedElementHandle ComponentElement = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent))
			{
				const bool bElementSelected = TypedElementSelectionSet->IsElementSelected(ComponentElement, FTypedElementIsSelectedOptions());
				return bElementSelected;
			}
		}
	}

	return false;
}

void UDataflowEditorMode::RefocusConstructionViewportClient()
{
	TSharedPtr<FDataflowConstructionViewportClient, ESPMode::ThreadSafe> PinnedVC = ConstructionViewportClient.Pin();
	if (PinnedVC.IsValid())
	{
		// This will happen in FocusViewportOnBox anyways; do it now to get a consistent end result
		PinnedVC->ToggleOrbitCamera(false);

		const FBox SceneBounds = SceneBoundingBox();
		constexpr bool bInstant = true;
		PinnedVC->FocusViewportOnBox(SceneBounds, bInstant);

		// Recompute near/far clip planes
		PinnedVC->SetConstructionViewMode(ConstructionViewMode);
	}
}

void UDataflowEditorMode::RefocusSimulationViewportClient()
{
	TSharedPtr<FDataflowSimulationViewportClient, ESPMode::ThreadSafe> PinnedVC = SimulationViewportClient.Pin();
	if (PinnedVC.IsValid())
	{
		// This will happen in FocusViewportOnBox anyways; do it now to get a consistent end result
		PinnedVC->ToggleOrbitCamera(false);

		const FBox SceneBounds = SceneBoundingBox();

		// 3D space
		PinnedVC->SetInitialViewTransform(ELevelViewportType::LVT_Perspective, FVector(0, 150, 200), FRotator(0, 0, 0), DEFAULT_ORTHOZOOM);

		constexpr bool bInstant = true;
		PinnedVC->FocusViewportOnBox(SceneBounds, bInstant);
	}
}

void UDataflowEditorMode::FirstTimeFocusConstructionViewport()
{
	// If this is the first time seeing a valid 2D or 3D mesh, refocus the camera on it.
	const bool bIsValid = (ConstructionScene->HasRenderableGeometry());
	const bool bIs2D = !ConstructionViewMode->IsPerspective();

	if (bIsValid)
	{
		if (bIs2D && bFirstValid2DMesh)
		{
			bFirstValid2DMesh = false;
			RefocusConstructionViewportClient();
		}
		else if (!bIs2D && bFirstValid3DMesh)
		{
			bFirstValid3DMesh = false;
			RefocusConstructionViewportClient();
		}
	}
}

void UDataflowEditorMode::FirstTimeFocusSimulationViewport()
{
	// If this is the first time seeing a valid 2D or 3D mesh, refocus the camera on it.
	const bool bIsValid = (SimulationScene->HasRenderableGeometry());

	if (bIsValid)
	{
		RefocusSimulationViewportClient();
	}
}

void UDataflowEditorMode::InitializeTargets(const TArray<TObjectPtr<UObject>>& ObjectsToEdit)
{
	UBaseCharacterFXEditorMode::InitializeTargets(ObjectsToEdit);

	// @todo(brice) : Consider initializing the Content here from the ObjectsToEdit

	// @todo(brice) : What are the ToolTargets storing?
	// ... for(ToolTarget& : ToolTargets){
	// ... UE::ToolTarget::GetDynamicMeshCopy(Target)
	// ... UE::ToolTarget::GetMaterialSet(Target).Materials for ConstructionScene->AddDynamicMeshComponent
	// ... }

	// @todo(michael) : do we need to update the construction scene?
	ConstructionScene->UpdateConstructionScene();
}

void UDataflowEditorMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);

	if (TSharedPtr<SDataflowGraphEditor> GraphEditor = DataflowGraphEditor.Pin())
	{
		// For now don't allow selection change once the tool has uncommitted changes
		// TODO: We might want to auto-accept unsaved changes and allow switching between nodes
		if (GetInteractiveToolsContext()->CanAcceptActiveTool())
		{
			GraphEditor->SetEnabled(false);
		}
		else
		{
			GraphEditor->SetEnabled(true);
		}
	}

	if (!NodeTypeForPendingToolStart.IsNone() && !GetToolManager()->HasActiveTool(EToolSide::Left))
	{
		const TSharedRef<FUICommandList> CommandList = Toolkit->GetToolkitCommands();
		const FDataflowEditorCommandsImpl& CommandInfos = FDataflowEditorCommandsImpl::Get();

		if (const TSharedPtr<const FUICommandInfo>* const Command = NodeTypeToToolCommandMap.Find(NodeTypeForPendingToolStart))
		{
			CommandList->TryExecuteAction(Command->ToSharedRef());
		}

		NodeTypeForPendingToolStart = FName();
	}


	if (bShouldRestartToolNextTick)
	{
		// If we ended the active tool in order to change view mode, restart it now

		const UDataflowBaseContent* const EditorContent = ConstructionScene->GetEditorContent();
		checkf(EditorContent, TEXT("Expected EditorContent in ConstructionScene"));

		if (!EditorContent->IsConstructionDirty())		// hold off restarting the tool until the scene finishes rebuilding
		{
			// First select the lone mesh in the construction scene if there is one
			if (bHadSingleSelectionBeforeToolShutdown)
			{
				const TArray<UDynamicMeshComponent*> DynamicMeshComponents = ConstructionScene->GetDynamicMeshComponents();
				if (DynamicMeshComponents.Num() == 1)
				{
					if (USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents())
					{
						SelectedComponents->Select(DynamicMeshComponents[0]);
					}
				}
			}

			// Now start the tool
			if (const TSharedPtr<const SDataflowGraphEditor> PinnedGraphEditor = DataflowGraphEditor.Pin())
			{
				const FGraphPanelSelectionSet& SelectedNodes = PinnedGraphEditor->GetSelectedNodes();
				if (SelectedNodes.Num() == 1)
				{
					StartToolForSelectedNode(*SelectedNodes.CreateConstIterator());
				}
			}

			bShouldRestartToolNextTick = false;
		}
	}
}

void UDataflowEditorMode::ConstructionViewportResized(FViewport* ConstructionViewport, uint32 /*Unused*/)
{
	// We'd like to call RefocusConstructionViewportClient() when the viewport is first created, however in Ortho mode the
	// viewport needs to have non-zero size for FocusViewportOnBox() to work properly. So we wait until the viewport is resized here.
	if (bShouldFocusConstructionView && ConstructionViewport && ConstructionViewport->GetSizeXY().X > 0 && ConstructionViewport->GetSizeXY().Y > 0)
	{
		RefocusConstructionViewportClient();
		bShouldFocusConstructionView = false;
	}
}

void UDataflowEditorMode::SimulationViewportResized(FViewport* SimulationViewport, uint32 /*Unused*/)
{
	// We'd like to call RefocusConstructionViewportClient() when the viewport is first created, however in Ortho mode the
	// viewport needs to have non-zero size for FocusViewportOnBox() to work properly. So we wait until the viewport is resized here.
	if (bShouldFocusSimulationView && SimulationViewport && SimulationViewport->GetSizeXY().X > 0 && SimulationViewport->GetSizeXY().Y > 0)
	{
		RefocusSimulationViewportClient();
		bShouldFocusSimulationView = false;
	}
}


FBox UDataflowEditorMode::SceneBoundingBox() const
{
	return ConstructionScene->GetBoundingBox();
}

FBox UDataflowEditorMode::SelectionBoundingBox() const
{
	// if Tool supports custom Focus box, use that first
	if (GetToolManager()->HasAnyActiveTool())
	{
		UInteractiveTool* const Tool = GetToolManager()->GetActiveTool(EToolSide::Mouse);
		IInteractiveToolCameraFocusAPI* const FocusAPI = Cast<IInteractiveToolCameraFocusAPI>(Tool);
		if (FocusAPI && FocusAPI->SupportsWorldSpaceFocusBox())
		{
			return FocusAPI->GetWorldSpaceFocusBox();
		}
	}

	// If the selection is on the GetBoundingBox is automatically computing the selection one
	FBox Bounds = ConstructionScene->GetBoundingBox();
	if (Bounds.IsValid)
	{
		return Bounds;
	}

	// Nothing selected, return the whole scene
	return SceneBoundingBox();
}

void UDataflowEditorMode::SetConstructionViewMode(const FName& NewViewModeName)
{
	if (NewViewModeName == ConstructionViewMode->GetName())
	{
		return;
	}

	const TObjectPtr<UInteractiveToolManager> ToolManager = GetInteractiveToolsContext()->ToolManager;
	checkf(ToolManager, TEXT("No valid ToolManager found for UDataflowEditorMode"));

	checkf(ConstructionScene, TEXT("Expected UDataflowEditorMode::ConstructionScene to have been initialized"));

	// Check if we have a single component selected. If we do, we will attempt to re-select it once the Construction Scene is rebuilt (if we have a tool running)
	bHadSingleSelectionBeforeToolShutdown = false;

	// Also check if we needed to shut down a running tool or not
	bool bToolWasShutDown = false;

	if (UInteractiveTool* const ActiveTool = ToolManager->GetActiveTool(EToolSide::Left))
	{
		if (const USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents())
		{
			if (SelectedComponents->Num() == 1)			// TODO: Extend this to handle multiple selected components
			{
				if (const UDynamicMeshComponent* const SelectedDynamicMeshComponent = Cast<UDynamicMeshComponent>(SelectedComponents->GetSelectedObject(0)))
				{
					if (ConstructionScene->GetDynamicMeshComponents().Contains(SelectedDynamicMeshComponent))
					{
						bHadSingleSelectionBeforeToolShutdown = true;
					}
				}
			}
		}

		const UInteractiveToolBuilder* const ActiveToolBuilder = ToolManager->GetActiveToolBuilder(EToolSide::Left);
		checkf(ActiveToolBuilder, TEXT("Found active tool with no active tool builder"));

		bool bToolCanHandleStateChange = false;

		if (const IDataflowEditorToolBuilder* const DataflowToolBuilder = Cast<IDataflowEditorToolBuilder>(ActiveToolBuilder))
		{
			FToolBuilderState SceneState;
			ToolManager->GetContextQueriesAPI()->GetCurrentSelectionState(SceneState);
			bToolCanHandleStateChange = DataflowToolBuilder->CanSceneStateChange(ActiveTool, SceneState);
		}

		if (!bToolCanHandleStateChange)
		{
			ToolManager->PostActiveToolShutdownRequest(ActiveTool, EToolShutdownType::Accept);
			bToolWasShutDown = true;
		}
	}

	const UE::Dataflow::FRenderingViewModeFactory& ViewModes = UE::Dataflow::FRenderingViewModeFactory::GetInstance();
	const UE::Dataflow::IDataflowConstructionViewMode* const NewMode = ViewModes.GetViewMode(NewViewModeName);
	if (!NewMode)
	{
		UE_LOG(LogChaos, Warning, TEXT("Warning : Unknown rendering view mode: %s"), *NewViewModeName.ToString());
		return;
	}

	// Do the actual view mode updates

	ConstructionViewMode = NewMode;
	ConstructionScene->GetEditorContent()->SetConstructionViewMode(ConstructionViewMode);
	ConstructionScene->UpdateConstructionScene();

	const TSharedPtr<FDataflowConstructionViewportClient> VC = ConstructionViewportClient.Pin();
	if (VC.IsValid())
	{
		VC->SetConstructionViewMode(ConstructionViewMode);
	}

	// If we are switching to a mode with a valid mesh for the first time, focus the camera on it
	FirstTimeFocusConstructionViewport();


	if (bToolWasShutDown)
	{
		// Tool restart must be done on the next tick because shutting down the current tool will cause the ConstructionView to be rebuilt next tick as well
		bShouldRestartToolNextTick = true;
	}
	else if (UInteractiveTool* const ActiveTool = ToolManager->GetActiveTool(EToolSide::Left))
	{
		// If there is a currently active tool, notify it that the scene has changed

		// First check if we previously had a single selected component before changing view modes. If so, and if there is now a single component in the construction scene, select it.
		// TODO: Extend this to handle multiple selected components

		if (bHadSingleSelectionBeforeToolShutdown)
		{
			const TArray<UDynamicMeshComponent*> DynamicMeshComponents = ConstructionScene->GetDynamicMeshComponents();
			if (DynamicMeshComponents.Num() == 1)
			{
				if (USelection* const SelectedComponents = GetModeManager()->GetSelectedComponents())
				{
					SelectedComponents->Select(DynamicMeshComponents[0]);
				}
			}
		}

		// Now notify the active tool that the SceneState is different
		UInteractiveToolBuilder* const ActiveToolBuilder = ToolManager->GetActiveToolBuilder(EToolSide::Left);
		checkf(ActiveToolBuilder, TEXT("Found active tool with no active tool builder"));

		if (IDataflowEditorToolBuilder* const DataflowToolBuilder = Cast<IDataflowEditorToolBuilder>(ActiveToolBuilder))
		{
			FToolBuilderState SceneState;
			ToolManager->GetContextQueriesAPI()->GetCurrentSelectionState(SceneState);
			DataflowToolBuilder->SceneStateChanged(ActiveTool, SceneState);
		}
	}
}

const UE::Dataflow::IDataflowConstructionViewMode* UDataflowEditorMode::GetConstructionViewMode() const
{
	return ConstructionViewMode;
}

bool UDataflowEditorMode::CanChangeConstructionViewModeTo(const FName& NewViewModeName) const
{
	if (!GetToolManager()->HasActiveTool(EToolSide::Left))
	{
		if (const TObjectPtr<UDataflowBaseContent>& EditorContent = ConstructionScene->GetEditorContent())
		{
			if (const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin())
			{
				if (const UEdGraphNode* const SelectedNode = PinnedDataflowGraphEditor->GetSingleSelectedNode())
				{
					if (const UDataflowEdNode* const SelectedDataflowEdNode = Cast<UDataflowEdNode>(SelectedNode))
					{
						if (const UE::Dataflow::IDataflowConstructionViewMode* const ViewMode = UE::Dataflow::FRenderingViewModeFactory::GetInstance().GetViewMode(NewViewModeName))
						{
							if (UE::Dataflow::CanRenderNodeOutput(*SelectedDataflowEdNode, *EditorContent, *ViewMode))
							{
								return true;
							}
						}
					}
				}
			}
		}

		return false;
	}


	// Check active tool to see if we can switch modes while the tool is running

	const UInteractiveToolBuilder* const ActiveToolBuilder = GetToolManager()->GetActiveToolBuilder(EToolSide::Left);
	checkf(ActiveToolBuilder, TEXT("No Active Tool Builder found despite having an Active Tool"));

	if (const IDataflowEditorToolBuilder* const DataflowToolBuilder = Cast<const IDataflowEditorToolBuilder>(ActiveToolBuilder))
	{
		const UEditorInteractiveToolsContext* const ConstructionToolsContext = GetInteractiveToolsContext();
		checkf(ConstructionToolsContext, TEXT("No Tools Context found in Dataflow Editor"));

		const UDataflowContextObject* const DataflowContextObject = ConstructionToolsContext->ContextObjectStore->FindContext<UDataflowContextObject>();
		checkf(DataflowContextObject, TEXT("No Dataflow Context Object found in ContextObjectStore, despite having an Active Tool. This should have been created by the time a tool is activated"));

		TArray<const UE::Dataflow::IDataflowConstructionViewMode*> SupportedViewModes;
		DataflowToolBuilder->GetSupportedConstructionViewModes(*DataflowContextObject, SupportedViewModes);

		if (const UE::Dataflow::IDataflowConstructionViewMode* const NewViewMode = UE::Dataflow::FRenderingViewModeFactory::GetInstance().GetViewMode(NewViewModeName))
		{
			return SupportedViewModes.Contains(NewViewMode);
		}
	}

	return false;
}


void UDataflowEditorMode::ToggleConstructionViewWireframe()
{
	check(false);
	bConstructionViewWireframe = !bConstructionViewWireframe;
	ConstructionScene->UpdateConstructionScene();
}

bool UDataflowEditorMode::CanSetConstructionViewWireframeActive() const
{
	if (!GetToolManager()->HasActiveTool(EToolSide::Left))
	{
		return true;
	}

	const UInteractiveToolBuilder* const ActiveToolBuilder = GetToolManager()->GetActiveToolBuilder(EToolSide::Left);
	checkf(ActiveToolBuilder, TEXT("No Active Tool Builder found despite having an Active Tool"));

	const IDataflowEditorToolBuilder* const DataflowToolBuilder = Cast<const IDataflowEditorToolBuilder>(ActiveToolBuilder);
	checkf(DataflowToolBuilder, TEXT("Cloth Editor has an active Tool Builder that does not implement IDataflowEditorToolBuilder"));
	return DataflowToolBuilder->CanSetConstructionViewWireframeActive();
}

void UDataflowEditorMode::SetConstructionViewportClient(TWeakPtr<FDataflowConstructionViewportClient, ESPMode::ThreadSafe> InViewportClient)
{
	ConstructionViewportClient = InViewportClient;

	TSharedPtr<FDataflowConstructionViewportClient> VC = ConstructionViewportClient.Pin();
	if (VC.IsValid())
	{
		VC->SetConstructionViewMode(ConstructionViewMode);
		VC->SetToolCommandList(ToolCommandList);

		if (VC->Viewport)
		{
			VC->Viewport->ViewportResizedEvent.AddUObject(this, &UDataflowEditorMode::ConstructionViewportResized);
		}
	}
}

void UDataflowEditorMode::SetSimulationViewportClient(TWeakPtr<FDataflowSimulationViewportClient, ESPMode::ThreadSafe> InViewportClient)
{
	SimulationViewportClient = InViewportClient;

	TSharedPtr<FDataflowSimulationViewportClient> VC = SimulationViewportClient.Pin();
	if (VC.IsValid())
	{
		if (VC->Viewport)
		{
			VC->Viewport->ViewportResizedEvent.AddUObject(this, &UDataflowEditorMode::SimulationViewportResized);
		}
	}
}

void UDataflowEditorMode::InitializeContextObject()
{
	check(ConstructionScene);

	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = ConstructionScene->GetEditorContent())
	{
		const UEditorInteractiveToolsContext* const ConstructionToolsContext = GetInteractiveToolsContext();

		UDataflowContextObject* ContextObject = ConstructionToolsContext->ContextObjectStore->FindContext<UDataflowContextObject>();
		if (!ContextObject)
		{
			ContextObject = EditorContent;
			ConstructionToolsContext->ContextObjectStore->AddContextObject(ContextObject);
		}

		check(ContextObject);
		ContextObject->SetConstructionViewMode(ConstructionViewMode);
	}
}

void UDataflowEditorMode::DeleteContextObject()
{
	UEditorInteractiveToolsContext* const ConstructionToolsContext = GetInteractiveToolsContext();
	if (UDataflowContextObject* ContextObject = ConstructionToolsContext->ContextObjectStore->FindContext<UDataflowContextObject>())
	{
		ConstructionToolsContext->ContextObjectStore->RemoveContextObject(ContextObject);
	}
}

void UDataflowEditorMode::SetDataflowGraphEditor(TSharedPtr<SDataflowGraphEditor> InGraphEditor)
{
	if (InGraphEditor)
	{
		DataflowGraphEditor = InGraphEditor;
		InitializeContextObject();
	}
	else
	{
		DeleteContextObject();
	}
}

void UDataflowEditorMode::StartToolForSelectedNode(const UObject* SelectedNode)
{
	if (const UDataflowEdNode* const EdNode = Cast<UDataflowEdNode>(SelectedNode))
	{
		if (const TSharedPtr<const FDataflowNode> DataflowNode = EdNode->GetDataflowNode())
		{
			const FName DataflowNodeType = DataflowNode->GetType();
			NodeTypeForPendingToolStart = DataflowNodeType;
		}
	}
}

void UDataflowEditorMode::OnDataflowNodeDeleted(const TSet<UObject*>& DeletedNodes)
{
	UEditorInteractiveToolsContext* const ToolsContext = GetInteractiveToolsContext();
	checkf(ToolsContext, TEXT("No valid ToolsContext found for UDataflowEditorMode"));
	const bool bCanCancel = ToolsContext->CanCancelActiveTool();
	ToolsContext->EndTool(bCanCancel ? EToolShutdownType::Cancel : EToolShutdownType::Completed);
}

UEdGraphNode* UDataflowEditorMode::GetSingleSelectedNodeWithOutputType(const FName& SelectedNodeOutputTypeName) const
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	UEdGraphNode* const SelectedNode = PinnedDataflowGraphEditor->GetSingleSelectedNode();
	if (!SelectedNode)
	{
		return nullptr;
	}

	 if (const UDataflowEdNode* const SelectedDataflowEdNode = Cast<UDataflowEdNode>(SelectedNode))
	 {
		 if (TSharedPtr<const FDataflowNode> SelectedDataflowNode = SelectedDataflowEdNode->GetDataflowNode())
		 {
			 for (const FDataflowOutput* const Output : SelectedDataflowNode->GetOutputs())
			 {
				 if (Output->GetType() == SelectedNodeOutputTypeName)
				 {
					 return SelectedNode;
				 }
			 }
		 }
	 }

	return nullptr;
}

UEdGraphNode* UDataflowEditorMode::CreateNewNode(const FName& NewNodeTypeName)
{
	const TSharedPtr<const SDataflowGraphEditor> PinnedDataflowGraphEditor = DataflowGraphEditor.Pin();
	if (!PinnedDataflowGraphEditor)
	{
		return nullptr;
	}

	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = ConstructionScene->GetEditorContent())
	{
		if (const TObjectPtr<UDataflow>& DataflowGraph = EditorContent->GetDataflowAsset())
		{
			const TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> NodeAction =
				FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(DataflowGraph, NewNodeTypeName);
			constexpr UEdGraphPin* FromPin = nullptr;
			constexpr bool bSelectNewNode = true;
			return NodeAction->PerformAction(DataflowGraph, FromPin, PinnedDataflowGraphEditor->GetPasteLocation(), bSelectNewNode);
		}
	}
	return nullptr;
}


UEdGraphNode* UDataflowEditorMode::CreateAndConnectNewNode(const FName& NewNodeTypeName, UEdGraphNode& UpstreamNode, const FName& ConnectionTypeName, const FName& NewNodeConnectionName)
{
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = ConstructionScene->GetEditorContent())
	{
		if (const TObjectPtr<UDataflow>& DataflowGraph = EditorContent->GetDataflowAsset())
		{
			// First find the specified output of the upstream node, plus any pins it's connected to

			UEdGraphPin* UpstreamNodeOutputPin = nullptr;
			TArray<UEdGraphPin*> ExistingNodeInputPins;

			const UDataflowEdNode* const UpstreamDataflowEdNode = CastChecked<UDataflowEdNode>(&UpstreamNode);
			const TSharedPtr<const FDataflowNode> UpstreamDataflowNode = UpstreamDataflowEdNode->GetDataflowNode();

			for (const FDataflowOutput* const Output : UpstreamDataflowNode->GetOutputs())
			{
				if (Output->GetType() == ConnectionTypeName)
				{
					UpstreamNodeOutputPin = UpstreamDataflowEdNode->FindPin(*Output->GetName().ToString(), EGPD_Output);
					ExistingNodeInputPins = UpstreamNodeOutputPin->LinkedTo;
					break;
				}
			}

			// Add the new node 

			UEdGraphNode* const NewEdNode = CreateNewNode(NewNodeTypeName);
			checkf(NewEdNode, TEXT("Failed to create a new node in the DataflowGraph"));

			UDataflowEdNode* const NewDataflowEdNode = CastChecked<UDataflowEdNode>(NewEdNode);
			const TSharedPtr<FDataflowNode> NewDataflowNode = NewDataflowEdNode->GetDataflowNode();

			// Re-wire the graph

			if (UpstreamNodeOutputPin)
			{
				UEdGraphPin* NewNodeInputPin = nullptr;
				for (const FDataflowInput* const NewNodeInput : NewDataflowNode->GetInputs())
				{
					if (NewNodeInput->GetType() == ConnectionTypeName && NewNodeInput->GetName() == NewNodeConnectionName)
					{
						NewNodeInputPin = NewDataflowEdNode->FindPin(*NewNodeInput->GetName().ToString(), EGPD_Input);
					}
				}

				UEdGraphPin* NewNodeOutputPin = nullptr;
				for (const FDataflowOutput* const NewNodeOutput : NewDataflowNode->GetOutputs())
				{
					if (NewNodeOutput->GetType() == ConnectionTypeName && NewNodeOutput->GetName() == NewNodeConnectionName)
					{
						NewNodeOutputPin = NewDataflowEdNode->FindPin(*NewNodeOutput->GetName().ToString(), EGPD_Output);
						break;
					}
				}

				check(NewNodeInputPin);
				check(NewNodeOutputPin);

				DataflowGraph->GetSchema()->TryCreateConnection(UpstreamNodeOutputPin, NewNodeInputPin);

				for (UEdGraphPin* DownstreamInputPin : ExistingNodeInputPins)
				{
					DataflowGraph->GetSchema()->TryCreateConnection(NewNodeOutputPin, DownstreamInputPin);
				}
			}

			DataflowGraph->NotifyGraphChanged();

			return NewEdNode;

		}
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE

