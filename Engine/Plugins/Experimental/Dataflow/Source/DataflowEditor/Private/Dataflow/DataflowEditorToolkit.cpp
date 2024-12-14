// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorToolkit.h"

#include "AdvancedPreviewScene.h"
#include "AdvancedPreviewSceneModule.h"
#include "AssetEditorModeManager.h"
#include "DetailCategoryBuilder.h"
#include "Animation/Skeleton.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEditor.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowCollectionSpreadSheetWidget.h"
#include "Dataflow/DataflowConstructionScene.h"
#include "Dataflow/DataflowConstructionViewportClient.h"
#include "Dataflow/DataflowEditorCollectionComponent.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowEditorModeToolkit.h"
#include "Dataflow/DataflowEditorModule.h"
#include "Dataflow/DataflowEditorModeUILayer.h"
#include "Dataflow/DataflowEditorUtil.h"
#include "Dataflow/DataflowConstructionViewport.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowEditorPreviewSceneBase.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "Dataflow/DataflowSimulationScene.h"
#include "Dataflow/DataflowSimulationVisualization.h"
#include "Dataflow/DataflowSchema.h"
#include "Dataflow/DataflowSkeletonView.h"
#include "Dataflow/DataflowSimulationViewportClient.h"
#include "DynamicMeshBuilder.h"
#include "EditorModeManager.h"
#include "EditorStyleSet.h"
#include "EditorViewportTabContent.h"
#include "EditorViewportLayout.h"
#include "EditorViewportCommands.h"
#include "EdModeInteractiveToolsContext.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Commands/GenericCommands.h"
#include "GameFramework/Actor.h"
#include "GraphEditorActions.h"
#include "IDetailCustomization.h"
#include "ISkeletonTree.h"
#include "IStructureDetailsView.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Selection.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Docking/SDockTab.h"
#include "GeometryCache.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DataflowEditorToolkit"

//DEFINE_LOG_CATEGORY_STATIC(EditorToolkitLog, Log, All);

bool bDataflowEnableSkeletonView = false;
FAutoConsoleVariableRef CVARDataflowEnableSkeletonView(TEXT("p.Dataflow.Editor.EnableSkeletonView"), bDataflowEnableSkeletonView,
	TEXT("Deprecated Tool! Allows the Dataflow editor to create a skeleton view that reflects the hierarchy and selection state of the construction viewport.[def:false]"));

const FName FDataflowEditorToolkit::GraphCanvasTabId(TEXT("DataflowEditor_GraphCanvas"));
const FName FDataflowEditorToolkit::NodeDetailsTabId(TEXT("DataflowEditor_NodeDetails"));
const FName FDataflowEditorToolkit::PreviewSceneTabId(TEXT("DataflowEditor_PreviewScene"));
const FName FDataflowEditorToolkit::SkeletonViewTabId(TEXT("DataflowEditor_SkeletonView"));
const FName FDataflowEditorToolkit::SelectionViewTabId_1(TEXT("DataflowEditor_SelectionView_1"));
const FName FDataflowEditorToolkit::SelectionViewTabId_2(TEXT("DataflowEditor_SelectionView_2"));
const FName FDataflowEditorToolkit::SelectionViewTabId_3(TEXT("DataflowEditor_SelectionView_3"));
const FName FDataflowEditorToolkit::SelectionViewTabId_4(TEXT("DataflowEditor_SelectionView_4"));
const FName FDataflowEditorToolkit::CollectionSpreadSheetTabId_1(TEXT("DataflowEditor_CollectionSpreadSheet_1"));
const FName FDataflowEditorToolkit::CollectionSpreadSheetTabId_2(TEXT("DataflowEditor_CollectionSpreadSheet_2"));
const FName FDataflowEditorToolkit::CollectionSpreadSheetTabId_3(TEXT("DataflowEditor_CollectionSpreadSheet_3"));
const FName FDataflowEditorToolkit::CollectionSpreadSheetTabId_4(TEXT("DataflowEditor_CollectionSpreadSheet_4"));
const FName FDataflowEditorToolkit::SimulationViewportTabId(TEXT("DataflowEditor_SimulationViewport"));
const FName FDataflowEditorToolkit::SimulationVisualizationTabId(TEXT("DataflowEditor_SimulationVisualizationTab"));

FDataflowEditorToolkit::FDataflowEditorToolkit(UAssetEditor* InOwningAssetEditor)
	: FBaseCharacterFXEditorToolkit(InOwningAssetEditor, FName("DataflowEditor"))
	, DataflowEditor(Cast< UDataflowEditor>(InOwningAssetEditor))
{

	// When saving, only prompt to checkout and save assets that are actually modified
	bCheckDirtyOnAssetSave = true;

	check(DataflowEditor);

	ConstructionDefaultLayout = FTabManager::NewLayout(FName("DataflowConstructionLayout03"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.8f)	// Relative width of (Tools Panel, Construction Viewport, Preview Viewport, Dataflow Graph Editor, Outliner) vs (Asset Details, Preview Scene Details, Dataflow Node Details)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
					->SetSizeCoefficient(0.60f)	// Relative height of (Tools Panel, Construction Viewport, Preview Viewport) vs (Dataflow Graph Editor, Outliner)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.1f)		// Relative width of (Tools Panel) vs (Construction Viewport, Preview Viewport)
						->SetExtensionId(UDataflowEditorUISubsystem::EditorSidePanelAreaName)
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.45f)		// Relative width of (Construction Viewport) vs (Tools Panel, Preview Viewport)
						->AddTab(ViewportTabID, ETabState::OpenedTab)
						->SetExtensionId("ViewportArea")
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.45f)		// Relative width of (Construction Viewport) vs (Tools Panel, Preview Viewport)
						->AddTab(SimulationViewportTabId, ETabState::OpenedTab)
						->SetExtensionId("ViewportArea")
						->SetHideTabWell(true)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
					->SetSizeCoefficient(0.40f)	// Relative height of (Dataflow Node Details) vs (Asset Details, Preview Scene Details)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.2f)
						->AddTab(CollectionSpreadSheetTabId_1, ETabState::OpenedTab)
						->SetExtensionId("CollectionSpreadSheetArea")
						->SetHideTabWell(false)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.8f)	// Relative height of (Dataflow Graph Editor, Outliner) vs (Tools Panel, Construction Viewport, Preview Viewport)
						->AddTab(GraphCanvasTabId, ETabState::OpenedTab)
						->SetExtensionId("GraphEditorArea")
						->SetHideTabWell(false)
						->SetForegroundTab(GraphCanvasTabId)
					)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.2f)	// Relative width of (Asset Details, Preview Scene Details, Dataflow Node Details) vs (Tools Panel, Construction Viewport, Preview Viewport, Dataflow Graph Editor, Outliner)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.65f)	// Relative height of (Asset Details, Preview Scene Details) vs (Dataflow Node Details)
					->AddTab(DetailsTabID, ETabState::OpenedTab)
					->AddTab(PreviewSceneTabId, ETabState::OpenedTab)
					->AddTab(SimulationVisualizationTabId, ETabState::OpenedTab)
					->SetExtensionId("DetailsArea")
					->SetHideTabWell(true)
					->SetForegroundTab(DetailsTabID)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)	// Relative height of (Dataflow Node Details) vs (Asset Details, Preview Scene Details)
					->AddTab(NodeDetailsTabId, ETabState::OpenedTab)
					->SetExtensionId("NodeDetailsArea")
					->SetHideTabWell(true)
				)
			)
		);

	SimulationDefaultLayout = FTabManager::NewLayout(FName("DataflowSimulationLayout02"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.8f)	// Relative width of (Tools Panel, Construction Viewport, Preview Viewport, Dataflow Graph Editor, Outliner) vs (Asset Details, Preview Scene Details, Dataflow Node Details)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
					->SetSizeCoefficient(0.60f)	// Relative height of (Tools Panel, Construction Viewport, Preview Viewport) vs (Dataflow Graph Editor, Outliner)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.1f)		// Relative width of (Tools Panel) vs (Construction Viewport, Preview Viewport)
						->SetExtensionId(UDataflowEditorUISubsystem::EditorSidePanelAreaName)
						->SetHideTabWell(true)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.9f)		// Relative width of (Construction Viewport) vs (Tools Panel, Preview Viewport)
						->AddTab(ViewportTabID, ETabState::ClosedTab)
						->AddTab(SimulationViewportTabId, ETabState::OpenedTab)
						->SetExtensionId("ViewportArea")
						->SetHideTabWell(false)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
					->SetSizeCoefficient(0.40f)	// Relative height of (Dataflow Node Details) vs (Asset Details, Preview Scene Details)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.2f)
						->AddTab(CollectionSpreadSheetTabId_1, ETabState::ClosedTab)
						->SetExtensionId("CollectionSpreadSheetArea")
						->SetHideTabWell(false)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.8f)	// Relative height of (Dataflow Graph Editor, Outliner) vs (Tools Panel, Construction Viewport, Preview Viewport)
						->AddTab(GraphCanvasTabId, ETabState::OpenedTab)
						->SetExtensionId("GraphEditorArea")
						->SetHideTabWell(false)
						->SetForegroundTab(GraphCanvasTabId)
					)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.2f)	// Relative width of (Asset Details, Preview Scene Details, Dataflow Node Details) vs (Tools Panel, Construction Viewport, Preview Viewport, Dataflow Graph Editor, Outliner)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.65f)	// Relative height of (Asset Details, Preview Scene Details) vs (Dataflow Node Details)
					->AddTab(DetailsTabID, ETabState::OpenedTab)
					->AddTab(PreviewSceneTabId, ETabState::OpenedTab)
					->SetExtensionId("DetailsArea")
					->SetHideTabWell(true)
					->SetForegroundTab(DetailsTabID)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)	// Relative height of (Dataflow Node Details) vs (Asset Details, Preview Scene Details)
					->AddTab(NodeDetailsTabId, ETabState::OpenedTab)
					->SetExtensionId("NodeDetailsArea")
					->SetHideTabWell(true)
				)
			)
		);

	if(TObjectPtr<UDataflowBaseContent>& EditorContent = DataflowEditor->GetEditorContent())
	{
		if(EditorContent->GetDataflowAsset() && EditorContent->GetDataflowAsset()->Type == EDataflowType::Simulation)
		{
			StandaloneDefaultLayout = SimulationDefaultLayout;
			bForceViewportTab = false;
		}
		else
		{
			StandaloneDefaultLayout = ConstructionDefaultLayout;
			bForceViewportTab = true;
		}
	}

	// Add any extenders specified by the UISubsystem
	// The extenders provide defined locations for FModeToolkit to attach
	// tool palette tabs and detail panel tabs
	LayoutExtender = MakeShared<FLayoutExtender>();
	FDataflowEditorModule* Module = &FModuleManager::LoadModuleChecked<FDataflowEditorModule>("DataflowEditor");
	Module->OnRegisterLayoutExtensions().Broadcast(*LayoutExtender);
	StandaloneDefaultLayout->ProcessExtensions(*LayoutExtender);

	FAdvancedPreviewScene::ConstructionValues PreviewSceneArgs;
	PreviewSceneArgs.bShouldSimulatePhysics = 1;
	PreviewSceneArgs.bCreatePhysicsScene = 1;
	
	ObjectScene = MakeUnique<FDataflowConstructionScene>(PreviewSceneArgs, DataflowEditor);
	SimulationScene = MakeShared<FDataflowSimulationScene>(PreviewSceneArgs, DataflowEditor);
}

FDataflowEditorToolkit::~FDataflowEditorToolkit()
{
	if (SimulationScene && SimulationScene->GetPreviewSceneDescription())
	{
		SimulationScene->GetPreviewSceneDescription()->DataflowSimulationSceneDescriptionChanged.Remove(OnSimulationSceneChangedDelegateHandle);
	}

	if (GraphEditor)
	{
		GraphEditor->OnSelectionChangedMulticast.Remove(OnSelectionChangedMulticastDelegateHandle);
		GraphEditor->OnNodeDeletedMulticast.Remove(OnNodeDeletedMulticastDelegateHandle);
	}

	if (NodeDetailsEditor)
	{
		NodeDetailsEditor->GetOnFinishedChangingPropertiesDelegate().Remove(OnFinishedChangingPropertiesDelegateHandle);
	}

	if (AssetDetailsEditor)
	{
		AssetDetailsEditor->OnFinishedChangingProperties().Remove(OnFinishedChangingAssetPropertiesDelegateHandle);
	}

	// We need to force the dataflow editor mode deletion now because otherwise the preview and rest-space worlds
	// will end up getting destroyed before the mode's Exit() function gets to run, and we'll get some
	// warnings when we destroy any mode actors.
	EditorModeManager->DestroyMode(UDataflowEditorMode::EM_DataflowEditorModeId);
	SimulationModeManager->DestroyMode(UDataflowEditorMode::EM_DataflowEditorModeId);
}

void FDataflowEditorToolkit::CreateEditorModeManager()
{
	// Setup the construction manager / scene
	FBaseCharacterFXEditorToolkit::CreateEditorModeManager();
	static_cast<FDataflowPreviewSceneBase*>(ObjectScene.Get())->GetDataflowModeManager()
		= StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager);

	// Setup the simulation manager / scene
	SimulationModeManager = MakeShared<FAssetEditorModeManager>();
	StaticCastSharedPtr<FAssetEditorModeManager>(SimulationModeManager)->SetPreviewScene(
		SimulationScene.Get());

	static_cast<FDataflowPreviewSceneBase*>(SimulationScene.Get())->GetDataflowModeManager()
		= StaticCastSharedPtr<FAssetEditorModeManager>(SimulationModeManager);
}

void FDataflowEditorToolkit::NotifyPreChange(FEditPropertyChain* PropertyAboutToChange)
{
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		ensure(EditorContent);
		if (UDataflow* DataflowAsset = EditorContent->GetDataflowAsset())
		{
			FDataflowEditorCommands::OnNotifyPropertyPreChange(NodeDetailsEditor, DataflowAsset, PropertyAboutToChange);
		}
	}
}

bool FDataflowEditorToolkit::CanOpenDataflowEditor(UObject* ObjectToEdit)
{
	if (const UClass* Class = ObjectToEdit->GetClass())
	{
		return (Class->FindPropertyByName(FName("DataflowAsset")) != nullptr) ;
	}
	return false;
}

bool FDataflowEditorToolkit::HasDataflowAsset(UObject* ObjectToEdit)
{
	if (const UClass* Class = ObjectToEdit->GetClass())
	{
		if (FProperty* Property = Class->FindPropertyByName(FName("DataflowAsset")))
		{
			return *Property->ContainerPtrToValuePtr<UDataflow*>(ObjectToEdit) != nullptr;
		}
	}
	return false;
}

UDataflow* FDataflowEditorToolkit::GetDataflowAsset(UObject* ObjectToEdit)
{
	UDataflow* DataflowObject = Cast<UDataflow>(ObjectToEdit);

	if (!DataflowObject)
	{
		if (const UClass* Class = ObjectToEdit->GetClass())
		{
			if (FProperty* Property = Class->FindPropertyByName(FName("DataflowAsset")))
			{
				DataflowObject = *Property->ContainerPtrToValuePtr<UDataflow*>(ObjectToEdit);
			}
		}
	}
	return DataflowObject;
}

const UDataflow* FDataflowEditorToolkit::GetDataflowAsset(const UObject* ObjectToEdit)
{
	const UDataflow* DataflowObject = Cast<UDataflow>(ObjectToEdit);

	if (!DataflowObject)
	{
		if (const UClass* Class = ObjectToEdit->GetClass())
		{
			if (const FProperty* Property = Class->FindPropertyByName(FName("DataflowAsset")))
			{
				DataflowObject = *Property->ContainerPtrToValuePtr<const UDataflow*>(ObjectToEdit);
			}
		}
	}
	return DataflowObject;
}

//~ Begin FBaseCharacterFXEditorToolkit overrides

FEditorModeID FDataflowEditorToolkit::GetEditorModeId() const
{
	return UDataflowEditorMode::EM_DataflowEditorModeId;
}

TObjectPtr<UDataflowBaseContent>& FDataflowEditorToolkit::GetEditorContent()
{
	return DataflowEditor->GetEditorContent();
}

const TObjectPtr<UDataflowBaseContent>& FDataflowEditorToolkit::GetEditorContent() const
{
	return DataflowEditor->GetEditorContent();
}

TArray<TObjectPtr<UDataflowBaseContent>>& FDataflowEditorToolkit::GetTerminalContents()
{
	return DataflowEditor->GetTerminalContents();
}

const TArray<TObjectPtr<UDataflowBaseContent>>& FDataflowEditorToolkit::GetTerminalContents() const
{
	return DataflowEditor->GetTerminalContents();
}

bool FDataflowEditorToolkit::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
	// Note: This needs a bit of adjusting, because currently OnRequestClose seems to be 
	// called multiple times when the editor itself is being closed. We can take the route 
	// of NiagaraScriptToolkit and remember when changes are discarded, but this can cause
	// issues if the editor close sequence is interrupted due to some other asset editor.

	UDataflowEditorMode* DataflowEdMode = Cast<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId));
	if (!DataflowEdMode) {
		// If we don't have a valid mode, because the OnRequestClose is currently being called multiple times,
		// simply return true because there's nothing left to do.
		return true;
	}

	// Give any active modes a chance to shutdown while the toolkit host is still alive
	// This is super important to do, otherwise currently opened tabs won't be marked as "closed".
	// This results in tabs not being properly recycled upon reopening the editor and tab
	// duplication for each opening event.
	GetEditorModeManager().ActivateDefaultMode();

	return FAssetEditorToolkit::OnRequestClose(InCloseReason);
}

void FDataflowEditorToolkit::PostInitAssetEditor()
{
	FBaseCharacterFXEditorToolkit::PostInitAssetEditor();
	
	auto SetCommonViewportClientOptions = [](FEditorViewportClient* Client)
	{
		// Normally the bIsRealtime flag is determined by whether the connection is remote, but our
		// tools require always being ticked.
		Client->SetRealtime(true);

		// Disable motion blur effects that cause our renders to "fade in" as things are moved
		Client->EngineShowFlags.SetTemporalAA(false);
		Client->EngineShowFlags.SetAntiAliasing(true);
		Client->EngineShowFlags.SetMotionBlur(false);

		// Disable the dithering of occluded portions of gizmos.
		Client->EngineShowFlags.SetOpaqueCompositeEditorPrimitives(true);

		// Disable hardware occlusion queries, which make it harder to use vertex shaders to pull materials
		// toward camera for z ordering because non-translucent materials start occluding themselves (once
		// the component bounds are behind the displaced geometry).
		Client->EngineShowFlags.SetDisableOcclusionQueries(true);

		// Default FOV of 90 degrees causes a fair bit of lens distortion, especially noticeable with smaller viewports
		Client->ViewFOV = 45.0;

		// Ortho has too many problems with rendering things, unfortunately, so we should use perspective.
		Client->SetViewportType(ELevelViewportType::LVT_Perspective);

		// Lit gives us the most options in terms of the materials we can use.
		Client->SetViewMode(EViewModeIndex::VMI_Lit);

		// If exposure isn't set to fixed, it will flash as we stare into the void
		Client->ExposureSettings.bFixed = true;

		// We need the viewport client to start out focused, or else it won't get ticked until
		// we click inside it.
		if(Client->Viewport)
		{
			Client->ReceivedFocus(Client->Viewport);
		}
	};
	SetCommonViewportClientOptions(ViewportClient.Get());
	SetCommonViewportClientOptions(SimulationViewportClient.Get());


	UDataflowEditorMode* const DataflowMode = CastChecked<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId));
	const TWeakPtr<FViewportClient> WeakConstructionViewportClient(ViewportClient);
	DataflowMode->SetConstructionViewportClient(StaticCastWeakPtr<FDataflowConstructionViewportClient>(WeakConstructionViewportClient));
	const TWeakPtr<FViewportClient> WeakSimulationViewportClient(SimulationViewportClient);
	DataflowMode->SetSimulationViewportClient(StaticCastWeakPtr<FDataflowSimulationViewportClient>(WeakSimulationViewportClient));

	FDataflowConstructionViewportClient* ConstructionViewportClient = static_cast<FDataflowConstructionViewportClient*>(ViewportClient.Get());
	OnConstructionSelectionChangedDelegateHandle = ConstructionViewportClient->OnSelectionChangedMulticast.AddSP(this, &FDataflowEditorToolkit::OnConstructionViewSelectionChanged);

	// Populate editor toolbar

	FName ParentToolbarName;
	const FName ToolBarName = GetToolMenuToolbarName(ParentToolbarName);
	UToolMenu* const AssetToolbar = UToolMenus::Get()->ExtendMenu(ToolBarName);
	FToolMenuSection& Section = AssetToolbar->FindOrAddSection("ClothTools");

	for (const TPair<FName, TSharedPtr<const FUICommandInfo>>& NodeAndAddCommand : DataflowMode->NodeTypeToAddNodeCommandMap)
	{
		ToolkitCommands->MapAction(NodeAndAddCommand.Value,
			FExecuteAction::CreateUObject(DataflowMode, &UDataflowEditorMode::AddNode, NodeAndAddCommand.Key),
			FCanExecuteAction::CreateUObject(DataflowMode, &UDataflowEditorMode::CanAddNode, NodeAndAddCommand.Key));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(NodeAndAddCommand.Value));
	}
}

void FDataflowEditorToolkit::InitializeEdMode(UBaseCharacterFXEditorMode* EdMode)
{
	UDataflowEditorMode* DataflowMode = Cast<UDataflowEditorMode>(EdMode);
	check(DataflowMode);
	DataflowMode->SetDataflowEditor(DataflowEditor);

	// We first set the preview scene in order to store the dynamic mesh elements
	// generated by the tools
	DataflowMode->SetDataflowConstructionScene(static_cast<FDataflowConstructionScene*>(ObjectScene.Get()));

	// Set of the graph editor to be able to add nodes
	DataflowMode->SetDataflowGraphEditor(GraphEditor);
	TArray<TObjectPtr<UObject>> ObjectsToEdit;
	OwningAssetEditor->GetObjectsToEdit(MutableView(ObjectsToEdit));
	DataflowMode->InitializeTargets(ObjectsToEdit);

	if (TSharedPtr<FModeToolkit> ModeToolkit = DataflowMode->GetToolkit().Pin())
	{
		FDataflowEditorModeToolkit* DataflowModeToolkit = static_cast<FDataflowEditorModeToolkit*>(ModeToolkit.Get());
		DataflowModeToolkit->SetConstructionViewportWidget(DataflowConstructionViewport);
		DataflowModeToolkit->SetSimulationViewportWidget(DataflowSimulationViewport);
	}

	// @todo(brice) : This used to crash when comnmented out. 
	FBaseCharacterFXEditorToolkit::InitializeEdMode(EdMode);
}

void FDataflowEditorToolkit::CreateEditorModeUILayer()
{
	FBaseCharacterFXEditorToolkit::CreateEditorModeUILayer();
}

void FDataflowEditorToolkit::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	FBaseCharacterFXEditorToolkit::GetSaveableObjects(OutObjects);

	if (ensure(GetEditorContent()))
	{
		if (UDataflow* DataflowAsset = GetEditorContent()->GetDataflowAsset())
		{
			check(DataflowAsset->IsAsset());
			OutObjects.AddUnique(DataflowAsset);
		}

		if(SimulationScene && SimulationScene->GetPreviewSceneDescription())
		{
			if(TObjectPtr<UChaosCacheCollection> CacheCollection = SimulationScene->GetPreviewSceneDescription()->CacheAsset)
			{
				OutObjects.AddUnique(CacheCollection);
			}
			if(TObjectPtr<UGeometryCache> GeometryCache = SimulationScene->GetPreviewSceneDescription()->GeometryCacheAsset)
			{
				OutObjects.AddUnique(GeometryCache);
			}
		}
	}
}

//~ End FBaseCharacterFXEditorToolkit overrides

class FDataflowPreviewSceneDescriptionCustomization : public IDetailCustomization
{
public:
	FDataflowPreviewSceneDescriptionCustomization(const TArray<UDataflowBaseContent*>& DataflowContents);

	virtual ~FDataflowPreviewSceneDescriptionCustomization() {}

	
	/**Customize details for the description */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	private :
		/** List of dataflow contents to preview */
		TMap<FString,TArray<UObject*>> ContentTypesObjects;
};

FDataflowPreviewSceneDescriptionCustomization::FDataflowPreviewSceneDescriptionCustomization(const TArray<UDataflowBaseContent*>& DataflowContents) :
	IDetailCustomization(), ContentTypesObjects()
{
	static const FString PreviewCategory = TEXT("Preview");
	TArray<UObject*>& PreviewObjects = ContentTypesObjects.FindOrAdd(PreviewCategory);
	for(UDataflowBaseContent* DataflowContent : DataflowContents)
	{
		if(DataflowContent)
		{
			PreviewObjects.Add(DataflowContent); 
		}
	}
}

void FDataflowPreviewSceneDescriptionCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FAddPropertyParams PropertyParams;
	PropertyParams.AllowChildren(true);
	PropertyParams.CreateCategoryNodes(false);
	PropertyParams.HideRootObjectNode(true);
	for(TPair<FString, TArray<UObject*>>& ContentTypeObjects : ContentTypesObjects)
	{
		DetailBuilder.EditCategory(*ContentTypeObjects.Key).AddExternalObjects(ContentTypeObjects.Value, EPropertyLocation::Common, PropertyParams);
	}
}

TSharedRef<class IDetailCustomization> FDataflowEditorToolkit::CustomizePreviewSceneDescription() const
{
	const TArray<UDataflowBaseContent*> SimulationContents  = TArray<UDataflowBaseContent*>{SimulationScene->GetEditorContent()};
	return MakeShareable(new FDataflowPreviewSceneDescriptionCustomization(SimulationContents));
}

//~ Begin FBaseAssetToolkit overrides

void FDataflowEditorToolkit::CreateWidgets()
{
	FBaseCharacterFXEditorToolkit::CreateWidgets();

	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		if (UDataflow* DataflowAsset = EditorContent->GetDataflowAsset())
		{
			NodeDetailsEditor = CreateNodeDetailsEditorWidget(EditorContent->GetDataflowOwner());
			if(EditorContent->GetDataflowOwner() != EditorContent->GetDataflowAsset())
			{
				AssetDetailsEditor = CreateAssetDetailsEditorWidget({EditorContent->GetDataflowOwner(),EditorContent->GetDataflowAsset()});
			}
			else
			{
				AssetDetailsEditor = CreateAssetDetailsEditorWidget({EditorContent->GetDataflowAsset()});
			}
			GraphEditor = CreateGraphEditorWidget(DataflowAsset, NodeDetailsEditor);

			// Synchronize the EditorContent's selected node with the GraphEditor
			UDataflowEdNode* const InitialSelectedNode = Cast<UDataflowEdNode>(GraphEditor->GetSingleSelectedNode());
			EditorContent->SetSelectedNode(InitialSelectedNode);

			CreateSimulationViewportClient();

			FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
			
			TArray<FAdvancedPreviewSceneModule::FDetailCustomizationInfo> DetailsCustomizations;
			DetailsCustomizations.Add({ UDataflowSimulationSceneDescription::StaticClass(),
				FOnGetDetailCustomizationInstance::CreateSP(const_cast<FDataflowEditorToolkit*>(this), &FDataflowEditorToolkit::CustomizePreviewSceneDescription) });
			
			AdvancedPreviewSettingsWidget = AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(SimulationScene.ToSharedRef(), 
				SimulationScene->GetPreviewSceneDescription(),
				DetailsCustomizations, 
				TArray<FAdvancedPreviewSceneModule::FPropertyTypeCustomizationInfo>(),
				TArray<FAdvancedPreviewSceneModule::FDetailDelegates>());
		}
	}
}

// Called from FBaseAssetToolkit::CreateWidgets. The delegate call path goes through FAssetEditorToolkit::InitAssetEditor
// and FBaseAssetToolkit::SpawnTab_Viewport.
AssetEditorViewportFactoryFunction FDataflowEditorToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction TempViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		TSharedRef<SDataflowConstructionViewport> Viewport = SAssignNew(DataflowConstructionViewport, SDataflowConstructionViewport, InArgs)
			.ViewportClient(StaticCastSharedPtr<FDataflowConstructionViewportClient>(ViewportClient));
		
		if (UDataflowEditorMode* const DataflowMode = Cast<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId)))
		{
			if (TSharedPtr<FModeToolkit> ModeToolkit = DataflowMode->GetToolkit().Pin())
			{
				if (FDataflowEditorModeToolkit* DataflowModeToolkit = static_cast<FDataflowEditorModeToolkit*>(ModeToolkit.Get()))
				{
					DataflowModeToolkit->SetConstructionViewportWidget(DataflowConstructionViewport);
				}
			}
		}
		return Viewport;
	};

	return TempViewportDelegate;
}

// Called from FBaseAssetToolkit::CreateWidgets to populate ViewportClient, but otherwise only used 
// in our own viewport delegate.
TSharedPtr<FEditorViewportClient> FDataflowEditorToolkit::CreateEditorViewportClient() const
{
	// Note that we can't reliably adjust the viewport client here because we will be passing it
	// into the viewport created by the viewport delegate we get from GetViewportDelegate(), and
	// that delegate may (will) affect the settings based on FAssetEditorViewportConstructionArgs,
	// namely ViewportType.
	// Instead, we do viewport client adjustment in PostInitAssetEditor().
	check(EditorModeManager.IsValid());
	TSharedPtr<FDataflowConstructionViewportClient> LocalConstructionClient = MakeShared<FDataflowConstructionViewportClient>(
	EditorModeManager.Get(), ObjectScene.Get(), true);
	LocalConstructionClient->SetDataflowEditorToolkit(StaticCastSharedRef<FDataflowEditorToolkit>(
		const_cast<FDataflowEditorToolkit*>(this)->AsShared()));
	return LocalConstructionClient;
}

void FDataflowEditorToolkit::CreateSimulationViewportClient()
{
	SimulationTabContent = MakeShareable(new FEditorViewportTabContent());
	SimulationViewportClient = MakeShared<FDataflowSimulationViewportClient>(SimulationModeManager.Get(),
		SimulationScene.Get(), false);
	
	SimulationViewportClient->SetDataflowEditorToolkit(StaticCastSharedRef<FDataflowEditorToolkit>(this->AsShared()));

	SimulationViewportDelegate = [this](FAssetEditorViewportConstructionArgs InArgs)
	{
		TSharedRef<SDataflowSimulationViewport> Viewport = SAssignNew(DataflowSimulationViewport, SDataflowSimulationViewport, InArgs)
			.ViewportClient(StaticCastSharedPtr<FDataflowSimulationViewportClient>(SimulationViewportClient))
			.CommandList(GetToolkitCommands().ToSharedPtr());;

		if (UDataflowEditorMode* const DataflowMode = Cast<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId)))
		{
			if (TSharedPtr<FModeToolkit> ModeToolkit = DataflowMode->GetToolkit().Pin())
			{
				if (FDataflowEditorModeToolkit* DataflowModeToolkit = static_cast<FDataflowEditorModeToolkit*>(ModeToolkit.Get()))
				{
					DataflowModeToolkit->SetSimulationViewportWidget(DataflowSimulationViewport);
				}
			}
		}
		return Viewport;
	};
}

//~ End FBaseAssetToolkit overrides

void FDataflowEditorToolkit::OnPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		ensure(EditorContent);
		if (UDataflow* DataflowAsset = EditorContent->GetDataflowAsset())
		{
			TSharedPtr<UE::Dataflow::FEngineContext> DataflowContext = EditorContent->GetDataflowContext();
			UE::Dataflow::FTimestamp LastNodeTimestamp = EditorContent->GetLastModifiedTimestamp();
			
			FDataflowEditorCommands::OnPropertyValueChanged(DataflowAsset, DataflowContext, LastNodeTimestamp, PropertyChangedEvent, SelectedDataflowNodes);

			EditorContent->SetDataflowContext(DataflowContext);
			EditorContent->SetLastModifiedTimestamp(LastNodeTimestamp);
		}
	}
}

void FDataflowEditorToolkit::OnAssetPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		ensure(EditorContent);	FDataflowEditorCommands::OnAssetPropertyValueChanged(EditorContent, PropertyChangedEvent);
	}
}

bool FDataflowEditorToolkit::OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* GraphNode, FText& OutErrorMessage)
{
	return FDataflowEditorCommands::OnNodeVerifyTitleCommit(NewText, GraphNode, OutErrorMessage);
}

void FDataflowEditorToolkit::OnNodeTitleCommitted(const FText& InNewText, ETextCommit::Type InCommitType, UEdGraphNode* GraphNode)
{
	FDataflowEditorCommands::OnNodeTitleCommitted(InNewText, InCommitType, GraphNode);
}



void FDataflowEditorToolkit::OnNodeSelectionChanged(const TSet<UObject*>& InNewSelection)
{
	//
	// Local helper lambdas
	//

	auto FindDataflowNodesInSet = [](const TSet<TObjectPtr<UObject>>& InSet) {
		TSet<TObjectPtr<UObject>> Results;
		for (UObject* Item : InSet)
		{
			if (Cast<UDataflowEdNode>(Item))
			{
				Results.Add(Item);
			}
		}
		return Results;
	};

	auto ResetListeners = [&ViewListeners = ViewListeners](UDataflowEdNode* Node = nullptr)
	{
		for (IDataflowViewListener* Listener : ViewListeners)
		{
			Listener->OnSelectedNodeChanged(nullptr);
		}
		if (Node)
		{
			for (IDataflowViewListener* Listener : ViewListeners)
			{
				Listener->OnSelectedNodeChanged(Node);
			}
		}
	};

	auto IsControlDown = [&DataflowEditor = GraphEditor]()
	{
		if (DataflowEditor)
		{
			return DataflowEditor->IsControlDown();
		}
		return false;
	};

	auto SelectComponentsInView = [&](TObjectPtr<UDataflowEdNode> Node)
	{
		TArray<AActor*> FoundActors;
		FDataflowConstructionScene* ConstructionScene = static_cast<FDataflowConstructionScene*>(ObjectScene.Get());
		if (USelection* SelectedComponents = ConstructionScene->GetDataflowModeManager()->GetSelectedComponents())
		{
			SelectedComponents->Modify();
			SelectedComponents->BeginBatchSelectOperation();

			TArray<TWeakObjectPtr<UObject>> SelectedObjects;
			const int32 NumSelected = SelectedComponents->GetSelectedObjects(SelectedObjects);
			for (TWeakObjectPtr<UObject> WeakObject : SelectedObjects)
			{
				if (WeakObject.IsValid())
				{
					if (UDataflowEditorCollectionComponent* ActorComponent = Cast< UDataflowEditorCollectionComponent>(WeakObject.Get()))
					{
						SelectedComponents->Deselect(ActorComponent);
						ActorComponent->PushSelectionToProxy();
					}
				}
			}

			if (TObjectPtr<AActor> RootActor = static_cast<FDataflowPreviewSceneBase*>(ObjectScene.Get())->GetRootActor())
			{
				for (UActorComponent* ActorComponent : RootActor->GetComponents())
				{
					if (UDataflowEditorCollectionComponent* Component = Cast<UDataflowEditorCollectionComponent>(ActorComponent))
					{
						if (Component->Node == Node)
						{
							SelectedComponents->Select(Component);
							Component->PushSelectionToProxy();
						}
					}
				}
			}
			SelectedComponents->EndBatchSelectOperation();
		}
	};

	//
	// Actual function
	// 
	
	// Despite this function's name, we might not have actually changed which node is selected
	bool bPrimarySelectionChanged = false;

	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent(); EditorContent->GetDataflowAsset())
	{
		auto AsObjectPointers = [](const TSet<UObject*>& Set) {
			TSet<TObjectPtr<UObject> > Objs; for (UObject* Elem : Set) Objs.Add(Elem);
			return Objs;
		};

		const TSet<TObjectPtr<UObject>> PreviouslySelectedNodes = SelectedDataflowNodes;
		for (UObject* const PreviouslySelectedNode : SelectedDataflowNodes)
		{
			if (UDataflowEdNode* const PreviouslySelectedEdNode = Cast<UDataflowEdNode>(PreviouslySelectedNode))
			{
				PreviouslySelectedEdNode->SetShouldRenderNode(false);
			}
		}

		// Only keep UDataflowEdNode from NewSelection
		TSet< TObjectPtr<UObject> > NodeSelection = FindDataflowNodesInSet(AsObjectPointers(InNewSelection));

		if (!NodeSelection.Num())
		{
			// The selection is empty. 
			ResetListeners();
			SelectedDataflowNodes = TSet<TObjectPtr<UObject>>();
			if (PrimarySelection) bPrimarySelectionChanged = true;
			PrimarySelection = nullptr;
		}
		else
		{
			TSet<TObjectPtr<UObject>> DeselectedNodes = SelectedDataflowNodes.Difference(NodeSelection);
			TSet<TObjectPtr<UObject>> StillSelectedNodes = SelectedDataflowNodes.Intersect(NodeSelection);
			TSet<TObjectPtr<UObject>> NewlySelectedNodes = NodeSelection.Difference(SelectedDataflowNodes);

			// Something has been removed
			if (DeselectedNodes.Num())
			{
				if (DeselectedNodes.Contains(PrimarySelection))
				{
					ResetListeners();

					if (PrimarySelection) bPrimarySelectionChanged = true;
					PrimarySelection = nullptr;

					// pick a new primary if nothing new was selected
					if (!NewlySelectedNodes.Num() && StillSelectedNodes.Num())
					{
						PrimarySelection = Cast< UDataflowEdNode>(StillSelectedNodes.Array()[0]);
						ResetListeners(PrimarySelection);
						bPrimarySelectionChanged = true;
					}
				}
			}

			// Something new has been selected.
			if (NewlySelectedNodes.Num() >= 1)
			{
				PrimarySelection = Cast< UDataflowEdNode>(NewlySelectedNodes.Array()[0]);
				ResetListeners(PrimarySelection);
				bPrimarySelectionChanged = true;
			}

			SelectedDataflowNodes = NodeSelection;
		}

		for (UObject* const SelectedNode : NodeSelection)
		{
			if (UDataflowEdNode* SelectedEdNode = Cast<UDataflowEdNode>(SelectedNode))
			{
				SelectedEdNode->SetShouldRenderNode(true);
			}
		}

		if (bPrimarySelectionChanged)
		{
			for (UObject* const PreviouslySelectedNode : PreviouslySelectedNodes)
			{
				if (UDataflowEdNode* PreviouslySelectedEdNode = Cast<UDataflowEdNode>(PreviouslySelectedNode))
				{
					PreviouslySelectedEdNode->SetShouldRenderNode(false);
				}
			}

			for (UObject* const SelectedNode : NodeSelection)
			{
				if (UDataflowEdNode* SelectedEdNode = Cast<UDataflowEdNode>(SelectedNode))
				{
					SelectedEdNode->SetShouldRenderNode(true);
				}
			}

			EditorContent->SetSelectedNode(nullptr);

			EditorContent->SetSelectedCollection(nullptr, /*bCollectionIsInput=*/ false);

			if( UDataflowEditorMode* const DataflowMode = Cast<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId)) )
			{
				// Close any running tool. OnNodeSingleClicked() will start a new tool if a new node was clicked.
				UEditorInteractiveToolsContext* const ToolsContext = DataflowMode->GetInteractiveToolsContext();
				checkf(ToolsContext, TEXT("No valid ToolsContext found for FDataflowEditorToolkit"));
				if (ToolsContext->HasActiveTool())
				{
					ToolsContext->EndTool(EToolShutdownType::Completed);
				}

				EditorContent->SetSelectedNode(PrimarySelection);

				// Call the node's OnSelected function. Some nodes use this to cache information from the inputs (e.g. FDataflowCollectionAddScalarVertexPropertyNode::CachedCollectionGroupNames)
				TSharedPtr<UE::Dataflow::FEngineContext> DataflowContext = EditorContent->GetDataflowContext();
				if (PrimarySelection && DataflowContext.IsValid())
				{
					if (const TSharedPtr<FDataflowNode> DataflowNode = PrimarySelection->GetDataflowNode())
					{
						// Update selected Collection in the ContextObject
						for (const FDataflowOutput* const Output : DataflowNode->GetOutputs())
						{
							if (Output->GetType() == FName(TEXT("FManagedArrayCollection")))
							{
								const FManagedArrayCollection DefaultValue;
								TSharedRef<FManagedArrayCollection> Collection = MakeShared<FManagedArrayCollection>(Output->GetValue<FManagedArrayCollection>(*DataflowContext, DefaultValue));
								constexpr bool bCollectionIsInput = false;
								EditorContent->SetSelectedCollection(Collection, bCollectionIsInput);
							}
						}
					}
				}
			}

			if (GetDataflowGraphEditor()->IsAltDown())
			{
				SelectComponentsInView(PrimarySelection);
			}
		}

		EditorContent->SetConstructionDirty(true);
	}

	//
	// Check if the current view mode can render the selected node. If not, try to find a view mode that can. 
	//

	if (UDataflowEditorMode* const DataflowMode = Cast<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId)))
	{
		bool bFoundViewMode = true;

		if (PrimarySelection && GetEditorContent())
		{
			if (!UE::Dataflow::CanRenderNodeOutput(*PrimarySelection, *GetEditorContent(), *DataflowMode->GetConstructionViewMode()))
			{
				// Selected node can't render with the current view mode. Check through available view modes and see if it can render with any of them

				bFoundViewMode = false;

				TArray<UE::Dataflow::FRenderingParameter> RenderingParameters = PrimarySelection->GetRenderParameters();
				for (const UE::Dataflow::FRenderingParameter& Param : RenderingParameters)
				{
					const FName NodeOutputTypeName = Param.Type;

					for (const TPair<FName, TUniquePtr<UE::Dataflow::IDataflowConstructionViewMode>>& ViewMode : UE::Dataflow::FRenderingViewModeFactory::GetInstance().GetViewModes())
					{
						check(ViewMode.Value.IsValid());

						const bool bCanRender = UE::Dataflow::CanRenderNodeOutput(*PrimarySelection, *GetEditorContent(), *ViewMode.Value);

						if (bCanRender)
						{
							DataflowMode->SetConstructionViewMode(ViewMode.Key);
							bFoundViewMode = true;
							break;
						}
					}

					if (bFoundViewMode)
					{
						break;
					}
				}
			}
		}

		if (!bFoundViewMode)
		{
			// TODO: Clear and disable View Mode Button. For now set default mode to the built-in 3D view mode.
			DataflowMode->SetConstructionViewMode(UE::Dataflow::FDataflowConstruction3DViewMode::Name);
		}
	}
}

void FDataflowEditorToolkit::OnNodeSingleClicked(UObject* ClickedNode) const
{
	UDataflowEditorMode* DataflowMode = Cast<UDataflowEditorMode>(EditorModeManager->GetActiveScriptableMode(UDataflowEditorMode::EM_DataflowEditorModeId));
	if (DataflowMode)
	{
		if (GraphEditor && GraphEditor->GetSingleSelectedNode() == ClickedNode)
		{
			// Start the corresponding tool
			DataflowMode->StartToolForSelectedNode(ClickedNode);
		}
	}
}


void FDataflowEditorToolkit::OnNodeDeleted(const TSet<UObject*>& NewSelection)
{
	for (UObject* Node : NewSelection)
	{
		if (SelectedDataflowNodes.Contains(Node))
		{
			SelectedDataflowNodes.Remove(Node);
		}
	}
}

void FDataflowEditorToolkit::OnConstructionViewSelectionChanged(const TArray<UPrimitiveComponent*>& SelectedComponents)
{
	for(IDataflowViewListener* Listener : ViewListeners)
	{
		Listener->OnConstructionViewSelectionChanged(SelectedComponents);
	}
}

void FDataflowEditorToolkit::OnFinishEvaluate()
{
	// Refresh graph display to update node output pin display (invalid or valid)
	GraphEditor->NotifyGraphChanged();
}

void FDataflowEditorToolkit::Tick(float DeltaTime)
{
	if (const TObjectPtr<UDataflowBaseContent> EditorContent = GetEditorContent())
	{
		if (EditorContent->GetDataflowAsset())
		{
			UE::Dataflow::FTimestamp InitTimeStamp = EditorContent->GetLastModifiedTimestamp();
			if (!EditorContent->GetDataflowContext())
			{
				EditorContent->SetDataflowContext(MakeShared<UE::Dataflow::FEngineContext>(EditorContent->GetDataflowOwner()));
				InitTimeStamp = UE::Dataflow::FTimestamp::Invalid;
			}

			// Update the list of dataflow terminal contents 
			DataflowEditor->UpdateTerminalContents(InitTimeStamp);

			// OnTick evaluation only pulls the terminal nodes. The other evaluations can be specific nodes.
			// We only evaluate multiple terminal nodes if the dataflow owner is a UDataflow (Owner == Asset)
			if (!GetTerminalContents().IsEmpty() && (EditorContent->GetDataflowOwner() == EditorContent->GetDataflowAsset()) && EditorContent->GetDataflowContext())
			{
				for (const TObjectPtr<UDataflowBaseContent>& TerminalContent : GetTerminalContents())
				{
					if (const UDataflow* const Dataflow = EditorContent->GetDataflowAsset())
					{
						if (const TSharedPtr<const UE::Dataflow::FGraph> Graph = Dataflow->GetDataflow())
						{
							const FName TerminalNodeName(TerminalContent->GetDataflowTerminal());
							const FDataflowNode* const Node = Graph->FindBaseNode(TerminalNodeName).Get();

							UE::Dataflow::FTimestamp TerminalNodeTimeStamp = InitTimeStamp;
							EvaluateNode(Node, nullptr, TerminalNodeTimeStamp);  // When Node is null, EvaluateNode falls back on the EditorContent terminal node

							// Take the Max of the existing time stamp, as other terminal nodes might have more recent invalidations
							const UE::Dataflow::FTimestamp LastModifiedTimestamp = FMath::Max(EditorContent->GetLastModifiedTimestamp(), TerminalNodeTimeStamp);
							
							constexpr bool bDontMakeDirty = false;
							EditorContent->SetLastModifiedTimestamp(LastModifiedTimestamp, bDontMakeDirty);
						}
					}
				}

				const bool bMakeDirty = (EditorContent->GetLastModifiedTimestamp() != InitTimeStamp);
				EditorContent->SetLastModifiedTimestamp(EditorContent->GetLastModifiedTimestamp(), bMakeDirty);
			}
			else
			{
				UE::Dataflow::FTimestamp TerminalNodeTimeStamp = InitTimeStamp;
				EvaluateNode(nullptr, nullptr, TerminalNodeTimeStamp);

				const bool bMakeDirty = (TerminalNodeTimeStamp != InitTimeStamp);
				EditorContent->SetLastModifiedTimestamp(TerminalNodeTimeStamp, bMakeDirty);
			}

			// Ensure the context object's selected node matches the selected node in the graph editor
			// TODO: Create an Editor Context Object that can just hold a reference to the graph editor, rather than keeping these in sync
			if (GraphEditor->GetNumberOfSelectedNodes() == 1)
			{
				const UDataflowEdNode* const ContextObjectSelectedNode = EditorContent->GetSelectedNode();
				const UDataflowEdNode* const EditorSelectedNode = Cast<UDataflowEdNode>(GraphEditor->GetSingleSelectedNode());
				ensure(EditorSelectedNode == ContextObjectSelectedNode);
			}

		}
	}
}

TStatId FDataflowEditorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDataflowEditorToolkit, STATGROUP_Tickables);
}

void FDataflowEditorToolkit::EvaluateNode(const FDataflowNode* Node, const FDataflowOutput* Output, UE::Dataflow::FTimestamp& InOutTimestamp)
{
	UE_LOG(LogChaosDataflow, VeryVerbose, TEXT("FDataflowEditorToolkit::EvaluateNode(): Node [%s], Output [%s]"), Node ? *Node->GetName().ToString() : TEXT("nullptr"), Output ? *Output->GetName().ToString() : TEXT("nullptr"));

	const bool bIsInPIEOrSimulate = GEditor->PlayWorld || GEditor->bIsSimulatingInEditor;
	if (!bIsInPIEOrSimulate)  // TODO: make this test optional for some toolkit
	{
		if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
		{
			if (EditorContent->GetDataflowAsset())
			{
				// If Node is null, the terminal node with the given name will be used instead
				FDataflowEditorCommands::EvaluateNode(*EditorContent->GetDataflowContext().Get(), InOutTimestamp, EditorContent->GetDataflowAsset(),
					Node, Output, EditorContent->GetDataflowTerminal(), EditorContent->GetTerminalAsset());
			}
		}
	}
}

TSharedRef<SDataflowGraphEditor> FDataflowEditorToolkit::CreateGraphEditorWidget(UDataflow* DataflowToEdit, TSharedPtr<IStructureDetailsView> InNodeDetailsEditor)
{
	ensure(DataflowToEdit);
	using namespace UE::Dataflow;

	const FDataflowEditorCommands::FGraphEvaluationCallback Evaluate =
		[this](const FDataflowNode* Node, const FDataflowOutput* Output)
		{
			if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
			{
				UE::Dataflow::FTimestamp LastNodeTimestamp = EditorContent->GetLastModifiedTimestamp();

				EvaluateNode(Node, Output, LastNodeTimestamp);

				EditorContent->SetLastModifiedTimestamp(LastNodeTimestamp);
			}

			//
			// Graph evaluation done
			//
			OnFinishEvaluate();
		};
	
	DataflowEditor->UpdateTerminalContents(FTimestamp::Invalid);
	
	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FDataflowEditorToolkit::OnNodeVerifyTitleCommit);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FDataflowEditorToolkit::OnNodeTitleCommitted);
	InEvents.OnNodeSingleClicked = SGraphEditor::FOnNodeSingleClicked::CreateSP(this, &FDataflowEditorToolkit::OnNodeSingleClicked);

	TSharedRef<SDataflowGraphEditor> NewGraphEditor = SNew(SDataflowGraphEditor, DataflowToEdit)
		.GraphToEdit(DataflowToEdit)
		.GraphEvents(InEvents)
		.DetailsView(InNodeDetailsEditor)
		.EvaluateGraph(Evaluate)
		.DataflowEditor(DataflowEditor);

	OnSelectionChangedMulticastDelegateHandle = NewGraphEditor->OnSelectionChangedMulticast.AddSP(this, &FDataflowEditorToolkit::OnNodeSelectionChanged);
	OnNodeDeletedMulticastDelegateHandle = NewGraphEditor->OnNodeDeletedMulticast.AddSP(this, &FDataflowEditorToolkit::OnNodeDeleted);

	return NewGraphEditor;
}

TSharedPtr<IStructureDetailsView> FDataflowEditorToolkit::CreateNodeDetailsEditorWidget(UObject* ObjectToEdit)
{
	ensure(ObjectToEdit);
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NotifyHook = this;
		DetailsViewArgs.bShowOptions = true;
		DetailsViewArgs.bShowModifiedPropertiesOption = false;
		DetailsViewArgs.bShowScrollBar = false;
	}

	FStructureDetailsViewArgs StructureViewArgs;
	{
		StructureViewArgs.bShowObjects = true;
		StructureViewArgs.bShowAssets = true;
		StructureViewArgs.bShowClasses = true;
		StructureViewArgs.bShowInterfaces = true;
	}
	TSharedPtr<IStructureDetailsView> LocalDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, nullptr);
	LocalDetailsView->GetDetailsView()->SetObject(ObjectToEdit);
	OnFinishedChangingPropertiesDelegateHandle = LocalDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &FDataflowEditorToolkit::OnPropertyValueChanged);

	return LocalDetailsView;
}

TSharedPtr<IDetailsView> FDataflowEditorToolkit::CreateAssetDetailsEditorWidget(const TArray<UObject*>& ObjectsToEdit)
{
	ensure(ObjectsToEdit.Num() > 0);
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.NotifyHook = this;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	}

	TSharedPtr<IDetailsView> LocalDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	LocalDetailsView->SetObjects(ObjectsToEdit, true);

	OnFinishedChangingAssetPropertiesDelegateHandle = LocalDetailsView->OnFinishedChangingProperties().AddSP(this, &FDataflowEditorToolkit::OnAssetPropertyValueChanged);

	return LocalDetailsView;

}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == DetailsTabID);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_AssetDetails_TabTitle", "Asset Details"))
		[
			AssetDetailsEditor->AsShared()
		];
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_SimulationViewport(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> DockableTab = SNew(SDockTab);
	if(SimulationTabContent)
	{
		SimulationTabContent->Initialize(SimulationViewportDelegate, DockableTab, SimulationViewportTabId.ToString());
	}
	return DockableTab;
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_PreviewScene(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PreviewSceneTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_PreviewScene_TabTitle", "PreviewScene"))
		[
			AdvancedPreviewSettingsWidget->AsShared()
		];
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == GraphCanvasTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_Dataflow_TabTitle", "Dataflow Graph"))
		[
			GraphEditor.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_NodeDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == NodeDetailsTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_NodeDetails_TabTitle", "Node Details"))
		[
			NodeDetailsEditor->GetWidget()->AsShared()
		];
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_SkeletonView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == SkeletonViewTabId);
	check(DataflowEditor);
	check(DataflowEditor->GetEditorContent());

	SkeletonEditorView = MakeShared<FDataflowSkeletonView>(DataflowEditor->GetEditorContent());
	ViewListeners.Add(SkeletonEditorView.Get());

	FSkeletonTreeArgs SkeletonTreeArgs;
	SkeletonTreeArgs.bShowBlendProfiles = false;
	SkeletonTreeArgs.bShowFilterMenu = true;
	SkeletonTreeArgs.bShowDebugVisualizationOptions = false;
	SkeletonTreeArgs.bAllowMeshOperations = false;
	SkeletonTreeArgs.bAllowSkeletonOperations = false;
	SkeletonTreeArgs.bHideBonesByDefault = false;
	SkeletonTreeArgs.OnSelectionChanged = FOnSkeletonTreeSelectionChanged::CreateSP(SkeletonEditorView.ToSharedRef(), &FDataflowSkeletonView::SkeletonViewSelectionChanged);
	SkeletonTreeArgs.ContextName = GetToolkitFName();

	TSharedPtr<ISkeletonTree> SkeletonEditor = SkeletonEditorView->CreateEditor(SkeletonTreeArgs);
	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_Outliner_TabTitle", "Outliner"))
		[
			SkeletonEditor.ToSharedRef()
		];
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_SelectionView(const FSpawnTabArgs& Args)
{
	//	check(Args.GetTabId().TabType == SelectionViewTabId_1);
	check(DataflowEditor);
	check(DataflowEditor->GetEditorContent());

	if (Args.GetTabId() == SelectionViewTabId_1)
	{
		DataflowSelectionView_1 = MakeShared<FDataflowSelectionView>(FDataflowSelectionView(DataflowEditor->GetEditorContent()));
		if (DataflowSelectionView_1.IsValid())
		{
			ViewListeners.Add(DataflowSelectionView_1.Get());
		}
	}
	else if (Args.GetTabId() == SelectionViewTabId_2)
	{
		DataflowSelectionView_2 = MakeShared<FDataflowSelectionView>(FDataflowSelectionView(DataflowEditor->GetEditorContent()));
		if (DataflowSelectionView_2.IsValid())
		{
			ViewListeners.Add(DataflowSelectionView_2.Get());
		}
	}
	else if (Args.GetTabId() == SelectionViewTabId_3)
	{
		DataflowSelectionView_3 = MakeShared<FDataflowSelectionView>(FDataflowSelectionView(DataflowEditor->GetEditorContent()));
		if (DataflowSelectionView_3.IsValid())
		{
			ViewListeners.Add(DataflowSelectionView_3.Get());
		}
	}
	else if (Args.GetTabId() == SelectionViewTabId_4)
	{
		DataflowSelectionView_4 = MakeShared<FDataflowSelectionView>(FDataflowSelectionView(DataflowEditor->GetEditorContent()));
		if (DataflowSelectionView_4.IsValid())
		{
			ViewListeners.Add(DataflowSelectionView_4.Get());
		}
	}

	TSharedPtr<SSelectionViewWidget> SelectionViewWidget;

	TSharedRef<SDockTab> DockableTab = SNew(SDockTab)
	[
		SAssignNew(SelectionViewWidget, SSelectionViewWidget)
	];

	if (SelectionViewWidget)
	{
		if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
		{
			if (Args.GetTabId() == SelectionViewTabId_1)
			{
				DataflowSelectionView_1->SetSelectionView(SelectionViewWidget);
			}
			else if (Args.GetTabId() == SelectionViewTabId_2)
			{
				DataflowSelectionView_2->SetSelectionView(SelectionViewWidget);
			}
			else if (Args.GetTabId() == SelectionViewTabId_3)
			{
				DataflowSelectionView_3->SetSelectionView(SelectionViewWidget);
			}
			else if (Args.GetTabId() == SelectionViewTabId_4)
			{
				DataflowSelectionView_4->SetSelectionView(SelectionViewWidget);
			}
		}
	}

	DockableTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FDataflowEditorToolkit::OnTabClosed));

	return DockableTab;
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_CollectionSpreadSheet(const FSpawnTabArgs& Args)
{
	check(DataflowEditor);
	check(DataflowEditor->GetEditorContent());

	if (Args.GetTabId() == CollectionSpreadSheetTabId_1)
	{
		DataflowCollectionSpreadSheet_1 = MakeShared<FDataflowCollectionSpreadSheet>(FDataflowCollectionSpreadSheet(DataflowEditor->GetEditorContent()));
		if (DataflowCollectionSpreadSheet_1.IsValid())
		{
			ViewListeners.Add(DataflowCollectionSpreadSheet_1.Get());
		}
	}
	else if (Args.GetTabId() == CollectionSpreadSheetTabId_2)
	{
		DataflowCollectionSpreadSheet_2 = MakeShared<FDataflowCollectionSpreadSheet>(FDataflowCollectionSpreadSheet(DataflowEditor->GetEditorContent()));
		if (DataflowCollectionSpreadSheet_2.IsValid())
		{
			ViewListeners.Add(DataflowCollectionSpreadSheet_2.Get());
		}
	}
	else if (Args.GetTabId() == CollectionSpreadSheetTabId_3)
	{
		DataflowCollectionSpreadSheet_3 = MakeShared<FDataflowCollectionSpreadSheet>(FDataflowCollectionSpreadSheet(DataflowEditor->GetEditorContent()));
		if (DataflowCollectionSpreadSheet_3.IsValid())
		{
			ViewListeners.Add(DataflowCollectionSpreadSheet_3.Get());
		}
	}
	else if (Args.GetTabId() == CollectionSpreadSheetTabId_4)
	{
		DataflowCollectionSpreadSheet_4 = MakeShared<FDataflowCollectionSpreadSheet>(FDataflowCollectionSpreadSheet(DataflowEditor->GetEditorContent()));
		if (DataflowCollectionSpreadSheet_4.IsValid())
		{
			ViewListeners.Add(DataflowCollectionSpreadSheet_4.Get());
		}
	}

	TSharedPtr<SCollectionSpreadSheetWidget> CollectionSpreadSheetWidget;

	TSharedRef<SDockTab> DockableTab = SNew(SDockTab)
	[
		SAssignNew(CollectionSpreadSheetWidget, SCollectionSpreadSheetWidget)
	];

	if (CollectionSpreadSheetWidget)
	{
		if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
		{
			if (Args.GetTabId() == CollectionSpreadSheetTabId_1)
			{
				DataflowCollectionSpreadSheet_1->SetCollectionSpreadSheet(CollectionSpreadSheetWidget);
			}
			else if (Args.GetTabId() == CollectionSpreadSheetTabId_2)
			{
				DataflowCollectionSpreadSheet_2->SetCollectionSpreadSheet(CollectionSpreadSheetWidget);
			}
			else if (Args.GetTabId() == CollectionSpreadSheetTabId_3)
			{
				DataflowCollectionSpreadSheet_3->SetCollectionSpreadSheet(CollectionSpreadSheetWidget);
			}
			else if (Args.GetTabId() == CollectionSpreadSheetTabId_4)
			{
				DataflowCollectionSpreadSheet_4->SetCollectionSpreadSheet(CollectionSpreadSheetWidget);
			}
		}
	}

	DockableTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FDataflowEditorToolkit::OnTabClosed));

	return DockableTab;
}

TSharedPtr<SWidget> FDataflowEditorToolkit::CreateSimulationVisualizationWidget()
{
	FMenuBuilder MenuBuilder(false, nullptr);

	using namespace UE::Dataflow;
	for (const TPair<FName, TUniquePtr<IDataflowSimulationVisualization>>& Visualization : FDataflowSimulationVisualizationRegistry::GetInstance().GetVisualizations())
	{
		Visualization.Value->ExtendSimulationVisualizationMenu(SimulationViewportClient, MenuBuilder);
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_SimulationVisualization(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SimulationVisualizationTab = SNew(SDockTab)
		.Label(LOCTEXT("SimulationVisualizationTitle", "Simulation Visualization"));

	SimulationVisualizationWidget = CreateSimulationVisualizationWidget();
	SimulationVisualizationTab->SetContent(SimulationVisualizationWidget.ToSharedRef());

	// Re-create the visualization panel when the simulation scene changes
	OnSimulationSceneChangedDelegateHandle = SimulationScene->GetPreviewSceneDescription()->DataflowSimulationSceneDescriptionChanged.AddLambda([this, SimulationVisualizationTab]()
	{
		SimulationVisualizationWidget = CreateSimulationVisualizationWidget();
		SimulationVisualizationTab->SetContent(SimulationVisualizationWidget.ToSharedRef());
	});

	return SimulationVisualizationTab;
}


void FDataflowEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	EditorMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_DataflowEditor", "Dataflow Editor"));
	const TSharedRef<FWorkspaceItem> SelectionViewWorkspaceMenuCategoryRef = EditorMenuCategory->AddGroup(LOCTEXT("WorkspaceMenu_SelectionView", "Selection View"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));
	const TSharedRef<FWorkspaceItem> CollectionSpreadSheetWorkspaceMenuCategoryRef = EditorMenuCategory->AddGroup(LOCTEXT("WorkspaceMenu_CollectionSpreadSheet", "Collection SpreadSheet"), FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));
	
	InTabManager->RegisterTabSpawner(ViewportTabID, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("DataflowViewportTab", "Construction Viewport"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(SimulationViewportTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SimulationViewport))
		.SetDisplayName(LOCTEXT("SimulationViewportTab", "Simulation Viewport"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsTabID, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_AssetDetails))
		.SetDisplayName(LOCTEXT("AssetDetailsTab", "Asset Details"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(PreviewSceneTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_PreviewScene))
		.SetDisplayName(LOCTEXT("PreviewSceneTab", "PreviewScene"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.PreviewScene"));
	
	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("DataflowTab", "Dataflow Graph"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(NodeDetailsTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_NodeDetails))
		.SetDisplayName(LOCTEXT("NodeDetailsTab", "Node Details"))
		.SetGroup(EditorMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	if (bDataflowEnableSkeletonView)
	{
		InTabManager->RegisterTabSpawner(SkeletonViewTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SkeletonView))
			.SetDisplayName(LOCTEXT("OutlinerTab", "Outliner"))
			.SetGroup(EditorMenuCategory.ToSharedRef())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.SkeletonHierarchy"));
	}

	InTabManager->RegisterTabSpawner(SelectionViewTabId_1, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SelectionView))
		.SetDisplayName(LOCTEXT("DataflowSelectionViewTab1", "Selection View 1"))
		.SetGroup(SelectionViewWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(SelectionViewTabId_2, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SelectionView))
		.SetDisplayName(LOCTEXT("DataflowSelectionViewTab2", "Selection View 2"))
		.SetGroup(SelectionViewWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(SelectionViewTabId_3, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SelectionView))
		.SetDisplayName(LOCTEXT("DataflowSelectionViewTab3", "Selection View 3"))
		.SetGroup(SelectionViewWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(SelectionViewTabId_4, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SelectionView))
		.SetDisplayName(LOCTEXT("DataflowSelectionViewTab4", "Selection View 4"))
		.SetGroup(SelectionViewWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(CollectionSpreadSheetTabId_1, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_CollectionSpreadSheet))
		.SetDisplayName(LOCTEXT("DataflowCollectionSpreadSheetTab1", "Collection SpreadSheet 1"))
		.SetGroup(CollectionSpreadSheetWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(CollectionSpreadSheetTabId_2, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_CollectionSpreadSheet))
		.SetDisplayName(LOCTEXT("DataflowCollectionSpreadSheetTab2", "Collection SpreadSheet 2"))
		.SetGroup(CollectionSpreadSheetWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(CollectionSpreadSheetTabId_3, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_CollectionSpreadSheet))
		.SetDisplayName(LOCTEXT("DataflowCollectionSpreadSheetTab3", "Collection SpreadSheet 3"))
		.SetGroup(CollectionSpreadSheetWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(CollectionSpreadSheetTabId_4, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_CollectionSpreadSheet))
		.SetDisplayName(LOCTEXT("DataflowCollectionSpreadSheetTab4", "Collection SpreadSheet 4"))
		.SetGroup(CollectionSpreadSheetWorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	InTabManager->RegisterTabSpawner(SimulationVisualizationTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SimulationVisualization))
		.SetDisplayName(LOCTEXT("SimulationVisualizationTabDisplayName", "Simulation Visualization"))
		.SetGroup(AssetEditorTabsCategory.ToSharedRef());

	
}

void FDataflowEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FBaseAssetToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(GraphCanvasTabId);
	InTabManager->UnregisterTabSpawner(NodeDetailsTabId);
	InTabManager->UnregisterTabSpawner(SkeletonViewTabId);
	InTabManager->UnregisterTabSpawner(SelectionViewTabId_1);
	InTabManager->UnregisterTabSpawner(SelectionViewTabId_2);
	InTabManager->UnregisterTabSpawner(SelectionViewTabId_3);
	InTabManager->UnregisterTabSpawner(SelectionViewTabId_4);
	InTabManager->UnregisterTabSpawner(CollectionSpreadSheetTabId_1);
	InTabManager->UnregisterTabSpawner(CollectionSpreadSheetTabId_2);
	InTabManager->UnregisterTabSpawner(CollectionSpreadSheetTabId_3);
	InTabManager->UnregisterTabSpawner(CollectionSpreadSheetTabId_4);
	InTabManager->UnregisterTabSpawner(SimulationViewportTabId);
}

void FDataflowEditorToolkit::OnTabClosed(TSharedRef<SDockTab> Tab)
{
	if (Tab->GetTabLabel().EqualTo(FText::FromString("Selection View 1")))
	{
		ViewListeners.Remove(DataflowSelectionView_1.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Selection View 2")))
	{
		ViewListeners.Remove(DataflowSelectionView_2.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Selection View 3")))
	{
		ViewListeners.Remove(DataflowSelectionView_3.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Selection View 4")))
	{
		ViewListeners.Remove(DataflowSelectionView_4.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Collection SpreadSheet 1")))
	{
		ViewListeners.Remove(DataflowCollectionSpreadSheet_1.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Collection SpreadSheet 2")))
	{
		ViewListeners.Remove(DataflowCollectionSpreadSheet_2.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Collection SpreadSheet 3")))
	{
		ViewListeners.Remove(DataflowCollectionSpreadSheet_3.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Collection SpreadSheet 4")))
	{
		ViewListeners.Remove(DataflowCollectionSpreadSheet_4.Get());
	}
	else if (Tab->GetTabLabel().EqualTo(FText::FromString("Skeleton View")))
	{
		ViewListeners.Remove(SkeletonEditorView.Get());
	}
}

FName FDataflowEditorToolkit::GetToolkitFName() const
{
	return FName("DataflowEditor");
}

FText FDataflowEditorToolkit::GetToolkitName() const
{
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		if (EditorContent->GetDataflowOwner())
		{
			return  GetLabelForObject(EditorContent->GetDataflowOwner());
		}
		else if (EditorContent->GetDataflowAsset())
		{
			return  GetLabelForObject(EditorContent->GetDataflowAsset());
		}
	}
	return  LOCTEXT("ToolkitName", "Empty Dataflow Editor");
}

FText FDataflowEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Dataflow Editor");
}

FText FDataflowEditorToolkit::GetToolkitToolTipText() const
{
	return  LOCTEXT("ToolkitToolTipText", "Dataflow Editor");
}

FString FDataflowEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Dataflow").ToString();
}

FLinearColor FDataflowEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

void FDataflowEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(SelectedDataflowNodes);
	Collector.AddReferencedObject(PrimarySelection);
}


#undef LOCTEXT_NAMESPACE
