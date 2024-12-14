// Copyright Epic Games, Inc. All Rights Reserved.

#include "TG_Exporter.h"

#include "TG_EditorTabs.h"
#include "CoreGlobals.h"
#include "Delegates/Delegate.h"
#include "TextureGraph.h"
#include "TG_Node.h"
#include "TG_Graph.h"
#include "TG_Parameter.h"
#include "Expressions/Output/TG_Expression_Output.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/Platform.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Layout/Margin.h"
#include "Misc/Attribute.h"
#include "SlotBase.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/ISlateStyle.h"
#include "Templates/SharedPointer.h"
#include "Textures/SlateIcon.h"
#include "Types/SlateEnums.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "AssetEditorViewportLayout.h"
#include "STG_EditorViewport.h"
#include "EditorViewportTabContent.h"
#include "AdvancedPreviewSceneModule.h"
#include "SPrimaryButton.h"
#include "TG_OutputSettings.h"
#include "STG_NodePreview.h"
#include "TG_HelperFunctions.h"
#include "PropertyEditorModule.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"

class SWidget;
class SWindow;

#define LOCTEXT_NAMESPACE "TextureGraphExporter"

struct FTG_ExporterCommands : public TCommands<FTG_ExporterCommands>
{
	FTG_ExporterCommands()
		: TCommands<FTG_ExporterCommands>(
			TEXT("TextureGraphExporter"), // Context name for fast lookup
			LOCTEXT("TextureGraphDebugger", "Texture Graph Exporter"), // Localized context name for displaying
			NAME_None, // Parent
			FCoreStyle::Get().GetStyleSetName() // Icon Style Set
		)
	{
	}

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

	TSharedPtr<FUICommandInfo> ShowOutputPreview;
	TSharedPtr<FUICommandInfo> Show3DPreview;
	TSharedPtr<FUICommandInfo> Show3DPreviewSettings;
	TSharedPtr<FUICommandInfo> ShowParameters;
	TSharedPtr<FUICommandInfo> ShowExportSettings;

};


struct FTG_ExporterImpl: public FTickableGameObject, public FGCObject
{
public:
	FTG_ExporterImpl();
	virtual ~FTG_ExporterImpl() override;

	/** Function registered with tab manager to create the Texture Graph Exporter */
	TSharedRef<SDockTab> CreateTGExporterTab(const FSpawnTabArgs& Args);

	void Cleanup();
	/** Sets the Texture Graph to be exported in the Exporter */
	void SetTextureGraphToExport(UTextureGraph* InTextureGraph);
	void OnGraphChanged(UTG_Graph* InGraph, UTG_Node* InNode, bool Tweaking);

	TSharedPtr<FTabManager>							TGExporterTabManager;
	TSharedPtr<FTabManager::FLayout>				TGExporterLayout;

	TObjectPtr<UTextureGraph>						TextureGraphPtr;
	TWeakPtr<STG_NodePreviewWidget>					NodePreviewPtr;
	
	TSharedPtr<class IDetailsView>					ParametersView;
	TSharedPtr<class IDetailsView>					ExportSettingsView;
	TSharedPtr<class IDetailsView>					PreviewSettingsView;
// Tracking the active viewports in this editor.
	TWeakPtr<class FEditorViewportTabContent>		ViewportTabContentPtr;

	TObjectPtr<UTG_Parameters>						Parameters;
	TObjectPtr<UTG_ExportSettings>					ExportSettings;
	FExportSettings                                 TargetExportSettings;
	
	TArray<TSharedPtr<FName>>						OutputNodesList;
	TSharedPtr<SComboBox<TSharedPtr<FName>>>		OutputNodesComboBoxWidget;
	UTG_Node*										SelectedNode = nullptr;
	// Inherited via FTickableGameObject
	virtual void									Tick(float DeltaTime) override;

	virtual ETickableTickType						GetTickableTickType() const override { return ETickableTickType::Always; }

	virtual bool									IsTickableWhenPaused() const override { return true; }

	virtual bool									IsTickableInEditor() const override { return true; }

	virtual TStatId									GetStatId() const override;

	// Inherited via FGCObject
	virtual void									AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString									GetReferencerName() const override { return TEXT("FTextureGraphExporter");}

private:
	TSharedRef<SDockTab>							SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab>							SpawnTab_ParameterDefaults(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab>							SpawnTab_NodePreview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab>							SpawnTab_ExportSettings(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab>							SpawnTab_PreviewSettings(const FSpawnTabArgs& Args);
	void 											RegisterTabSpawners(const TSharedPtr<FTabManager>);
	void  											UnregisterTabSpawners(const TSharedPtr<FTabManager>);
	TSharedPtr<class STG_EditorViewport>			GetEditorViewport() const;
	void 											SetViewportPreviewMesh();	
	bool 											SetPreviewAsset(UObject* InAsset);
	TSharedRef<class IDetailsView>					GetPreviewSettingsView() const { return PreviewSettingsView.ToSharedRef(); }
	TSharedRef<class IDetailsView>					GetExportSettingsView() const { return ExportSettingsView.ToSharedRef(); }
	TSharedRef<class IDetailsView>					GetParametersView() const { return ParametersView.ToSharedRef(); }
	void											OnViewportMaterialChanged();
	void											OnMaterialMappingChanged();
	void											UpdateExportSettingsUI();
	void											UpdateParametersUI();
	virtual void									RefreshViewport();
	void											OnRenderingDone(UMixInterface* TextureGraph, const FInvalidationDetails* Details);
	TSharedRef<SWidget>								GenerateOutputComboItem(TSharedPtr<FName> String);
	void											OnOutputSelectionChanged(TSharedPtr<FName> String, ESelectInfo::Type Arg);
	FReply											OnExportClicked(EAppReturnType::Type ButtonID);

	// prevent copying:
	FTG_ExporterImpl(const FTG_ExporterImpl&);
	FTG_ExporterImpl(FTG_ExporterImpl&&);
	FTG_ExporterImpl& operator=(FTG_ExporterImpl const&);
	FTG_ExporterImpl& operator=(FTG_ExporterImpl&&);
};

void FTG_ExporterCommands::RegisterCommands()
{
	UI_COMMAND(ShowOutputPreview, "Node Preview", "Toggles visibility of the Output Preview", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(Show3DPreview, "3D Preview", "Toggles visibility of the 3D Preview window", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(Show3DPreviewSettings, "3D Preview Settings", "Toggles visibility of the 3D Preview Settings window", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(ShowParameters, "Parameters", "Toggles visibility of the Parameters window", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(ShowExportSettings, "Export Settings", "Toggles visibility of the Export Settings window", EUserInterfaceActionType::Check, FInputChord());
}
FTG_ExporterImpl::FTG_ExporterImpl()
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();

	FTG_ExporterCommands::Register();
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FTG_EditorTabs::TextureExporterTabId, FOnSpawnTab::CreateRaw(this, &FTG_ExporterImpl::CreateTGExporterTab))
		.SetDisplayName(NSLOCTEXT("TextureGraphExporter", "TabTitle", "Texture Graph Exporter"))
		.SetTooltipText(NSLOCTEXT("TextureGraphExporter", "TooltipText", "Open the Texture Graph Exporter tab."))
		.SetGroup(MenuStructure.GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D"));
}

FTG_ExporterImpl::~FTG_ExporterImpl()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FTG_EditorTabs::TextureExporterTabId);
	}

	if (TGExporterTabManager.IsValid())
	{
		UnregisterTabSpawners(TGExporterTabManager);
		FGlobalTabmanager::Get()->UnregisterTabSpawner(FTG_EditorTabs::TextureExporterTabId);
		TGExporterLayout = TSharedPtr<FTabManager::FLayout>();
		TGExporterTabManager = TSharedPtr<FTabManager>();
		
		
		Cleanup();
		
		ExportSettingsView.Reset();
		PreviewSettingsView.Reset();
		ParametersView.Reset();
		
		// cleanup UI
		if (Parameters->IsValidLowLevelFast())
		{
			Parameters->Parameters.Empty();
			Parameters = nullptr;
		}
		if (ExportSettings->IsValidLowLevelFast())
		{
			ExportSettings->OutputExpressionsInfos.Empty();
			ExportSettings = nullptr;
		}
		OutputNodesComboBoxWidget.Reset();
	}
	FTG_ExporterCommands::Unregister();
}

void FTG_ExporterImpl::Cleanup()
{
	if (TextureGraphPtr->IsValidLowLevelFast())
	{
		// cleanup events
		TextureGraphPtr->GetSettings()->GetViewportSettings().OnViewportMaterialChangedEvent.RemoveAll(this);
		TextureGraphPtr->GetSettings()->GetViewportSettings().OnMaterialMappingChangedEvent.RemoveAll(this);
		TextureGraphPtr->GetSettings()->OnPreviewMeshChangedEvent.RemoveAll(this);
		TextureGraphPtr->Graph()->OnGraphChangedDelegate.RemoveAll(this);
		TextureGraphPtr->OnRenderDone.Unbind();
		TextureGraphPtr = nullptr;
	}

	OutputNodesList.Empty();
	SelectedNode = nullptr;
}
TSharedRef<SDockTab> FTG_ExporterImpl::CreateTGExporterTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> NomadTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(NSLOCTEXT("TextureGraphExporter", "TabTitle", "Texture Graph Exporter"));

	TGExporterTabManager = FGlobalTabmanager::Get()->NewTabManager(NomadTab);
	// on persist layout will handle saving layout if the editor is shut down:
	TGExporterTabManager->SetOnPersistLayout(
		FTabManager::FOnPersistLayout::CreateStatic(
			[](const TSharedRef<FTabManager::FLayout>& InLayout)
			{
				if (InLayout->GetPrimaryArea().Pin().IsValid())
				{
					FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, InLayout);
				}
			}
		)
	);


	TWeakPtr<FTabManager> TGExporterTabManagerWeak = TGExporterTabManager;
	// On tab close will save the layout if the exporter window itself is closed,
	// this handler also cleans up any floating controls. If we don't close
	// all areas we need to add some logic to the tab manager to reuse existing tabs:
	NomadTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateStatic(
		[](TSharedRef<SDockTab> Self, TWeakPtr<FTabManager> TabManager)
		{
			TSharedPtr<FTabManager> OwningTabManager = TabManager.Pin();
			if (OwningTabManager.IsValid())
			{
				FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, OwningTabManager->PersistLayout());
				OwningTabManager->CloseAllAreas();
			}
		}
		, TGExporterTabManagerWeak
	));
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs ParameterViewArgs;
	ParameterViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	ParameterViewArgs.bHideSelectionTip = true;
	ParameterViewArgs.ColumnWidth = 0.70;
	ParametersView = PropertyEditorModule.CreateDetailView(ParameterViewArgs);

	// Settings details view
	FDetailsViewArgs ExportSettingsViewArgs;
	ExportSettingsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	ExportSettingsViewArgs.bHideSelectionTip = true;
	ExportSettingsView = PropertyEditorModule.CreateDetailView(ExportSettingsViewArgs);

	// Settings details view
	FDetailsViewArgs SettingsViewArgs;
	SettingsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	SettingsViewArgs.bHideSelectionTip = true;
	PreviewSettingsView = PropertyEditorModule.CreateDetailView(SettingsViewArgs);
	
	RegisterTabSpawners(TGExporterTabManager);

	
	TGExporterLayout = FTabManager::NewLayout("Standalone_TextureGraphExporter_Layout_v1")
	->AddArea
	(
		FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.25f)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(FTG_EditorTabs::ParameterDefaultsTabId, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(FTG_EditorTabs::OutputTabId, ETabState::OpenedTab)
					)
				)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.25f)
					->Split
					(
						FTabManager::NewStack()
						->SetHideTabWell(true)
						->AddTab(FTG_EditorTabs::NodePreviewTabId, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->AddTab(FTG_EditorTabs::ViewportTabId, ETabState::OpenedTab)
						->AddTab(FTG_EditorTabs::PreviewSettingsTabId, ETabState::OpenedTab)
						->SetForegroundTab(FTG_EditorTabs::ViewportTabId)
					)
				)
			)
		);

	
	TGExporterLayout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, TGExporterLayout.ToSharedRef());
	
	TSharedRef<SWidget> TabContents = TGExporterTabManager->RestoreFrom(TGExporterLayout.ToSharedRef(), TSharedPtr<SWindow>()).ToSharedRef();
	
	// build command list for tab restoration menu:
	TSharedPtr<FUICommandList> CommandList = MakeShareable(new FUICommandList());
	
	TWeakPtr<FTabManager> TGExportManagerWeak = TGExporterTabManager;
	
	const auto ToggleTabVisibility = [](TWeakPtr<FTabManager> InTGExportManagerWeak, FName InTabName)
	{
		TSharedPtr<FTabManager> InTGExportManager = InTGExportManagerWeak.Pin();
		if (InTGExportManager.IsValid())
		{
			TSharedPtr<SDockTab> ExistingTab = InTGExportManager->FindExistingLiveTab(InTabName);
			if (ExistingTab.IsValid())
			{
				ExistingTab->RequestCloseTab();
			}
			else
			{
				InTGExportManager->TryInvokeTab(InTabName);
			}
		}
	};
	
	const auto IsTabVisible = [](TWeakPtr<FTabManager> InTGExportManagerWeak, FName InTabName)
	{
		TSharedPtr<FTabManager> InTGExportManager = InTGExportManagerWeak.Pin();
		if (InTGExportManager.IsValid())
		{
			return InTGExportManager->FindExistingLiveTab(InTabName).IsValid();
		}
		return false;
	};
	
	bool bViewportIsOff = !ViewportTabContentPtr.IsValid();
	TSharedPtr<SDockTab> ViewportTab; 
	// check here if 3d viewport is turned off, we need to turn it on temporarily to initialize our systems correctly
	if (bViewportIsOff)
	{
		ViewportTab = TGExporterTabManager->TryInvokeTab(FTG_EditorTabs::ViewportTabId);	
	}
	
	// Set the preview mesh for the material.  This call must occur after the toolbar is initialized.
	SetViewportPreviewMesh();

	CommandList->MapAction(
		FTG_ExporterCommands::Get().Show3DPreview,
		FExecuteAction::CreateStatic(
			ToggleTabVisibility,
			TGExportManagerWeak,
			FTG_EditorTabs::ViewportTabId
		),
		FCanExecuteAction::CreateStatic(
			[]() { return true; }
		),
		FIsActionChecked::CreateStatic(
			IsTabVisible,
			TGExportManagerWeak,
			FTG_EditorTabs::ViewportTabId
		)
	);
	
	CommandList->MapAction(
		FTG_ExporterCommands::Get().Show3DPreviewSettings,
		FExecuteAction::CreateStatic(
			ToggleTabVisibility,
			TGExportManagerWeak,
			FTG_EditorTabs::PreviewSettingsTabId
		),
		FCanExecuteAction::CreateStatic(
			[]() { return true; }
		),
		FIsActionChecked::CreateStatic(
			IsTabVisible,
			TGExportManagerWeak,
			FTG_EditorTabs::PreviewSettingsTabId
		)
	);

	CommandList->MapAction(
			FTG_ExporterCommands::Get().ShowParameters,
			FExecuteAction::CreateStatic(
				ToggleTabVisibility,
				TGExportManagerWeak,
				FTG_EditorTabs::ParameterDefaultsTabId
			),
			FCanExecuteAction::CreateStatic(
				[]() { return true; }
			),
			FIsActionChecked::CreateStatic(
				IsTabVisible,
				TGExportManagerWeak,
				FTG_EditorTabs::ParameterDefaultsTabId
			)
		);

	CommandList->MapAction(
			FTG_ExporterCommands::Get().ShowOutputPreview,
			FExecuteAction::CreateStatic(
				ToggleTabVisibility,
				TGExportManagerWeak,
				FTG_EditorTabs::NodePreviewTabId
			),
			FCanExecuteAction::CreateStatic(
				[]() { return true; }
			),
			FIsActionChecked::CreateStatic(
				IsTabVisible,
				TGExportManagerWeak,
				FTG_EditorTabs::NodePreviewTabId
			)
		);

	CommandList->MapAction(
			FTG_ExporterCommands::Get().ShowExportSettings,
			FExecuteAction::CreateStatic(
				ToggleTabVisibility,
				TGExportManagerWeak,
				FTG_EditorTabs::OutputTabId
			),
			FCanExecuteAction::CreateStatic(
				[]() { return true; }
			),
			FIsActionChecked::CreateStatic(
				IsTabVisible,
				TGExportManagerWeak,
				FTG_EditorTabs::OutputTabId
			)
		);
	
	FMenuBarBuilder MenuBarBuilder(CommandList);
	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("WindowMenuLabel", "Window"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateLambda([](FMenuBuilder& Builder) {
			Builder.AddMenuEntry(FTG_ExporterCommands::Get().ShowOutputPreview);
			Builder.AddMenuEntry(FTG_ExporterCommands::Get().ShowParameters);
			Builder.AddMenuEntry(FTG_ExporterCommands::Get().Show3DPreviewSettings);
			Builder.AddMenuEntry(FTG_ExporterCommands::Get().Show3DPreview);
			Builder.AddMenuEntry(FTG_ExporterCommands::Get().ShowExportSettings);
			})
	);
	
	
	TSharedRef<SWidget> MenuBarWidget = MenuBarBuilder.MakeWidget();
	
	NomadTab->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MenuBarWidget
		]
		+SVerticalBox::Slot()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(0.f, 2.f))
			[
				TabContents
			]
		]
	);
	
	
	// Tell tab-manager about the multi-box for platforms with a global menu bar
	TGExporterTabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuBarWidget);
	
	return NomadTab;
}


void FTG_ExporterImpl::RegisterTabSpawners(const TSharedPtr<FTabManager> InTabManager)
{
	InTabManager->RegisterTabSpawner(FTG_EditorTabs::ViewportTabId, FOnSpawnTab::CreateRaw(this, &FTG_ExporterImpl::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "3D Preview"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(FTG_EditorTabs::ParameterDefaultsTabId, FOnSpawnTab::CreateRaw(this, &FTG_ExporterImpl::SpawnTab_ParameterDefaults))
		.SetDisplayName(LOCTEXT("ParametersTab", "Parameters"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FTG_EditorTabs::NodePreviewTabId, FOnSpawnTab::CreateRaw(this, &FTG_ExporterImpl::SpawnTab_NodePreview))
		.SetDisplayName(LOCTEXT("NodePreviewTab", "Node Preview"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(FTG_EditorTabs::PreviewSettingsTabId, FOnSpawnTab::CreateRaw(this, &FTG_ExporterImpl::SpawnTab_PreviewSettings))
		.SetDisplayName(LOCTEXT("PreviewSettingsTab", "3D Preview Settings"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(FTG_EditorTabs::OutputTabId, FOnSpawnTab::CreateRaw(this, &FTG_ExporterImpl::SpawnTab_ExportSettings))
		.SetDisplayName(LOCTEXT("ExportSettingsTab", "Export Settings"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FTG_ExporterImpl::UnregisterTabSpawners(const TSharedPtr<FTabManager> InTabManager)
{
	InTabManager->UnregisterTabSpawner(FTG_EditorTabs::ViewportTabId);
	InTabManager->UnregisterTabSpawner(FTG_EditorTabs::ParameterDefaultsTabId);
	InTabManager->UnregisterTabSpawner(FTG_EditorTabs::NodePreviewTabId);
	InTabManager->UnregisterTabSpawner(FTG_EditorTabs::OutputTabId);
	InTabManager->UnregisterTabSpawner(FTG_EditorTabs::PreviewSettingsTabId);
}
TSharedRef<SDockTab> FTG_ExporterImpl::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FTG_EditorTabs::PreviewSettingsTabId);

	TSharedPtr<SDockTab> SettingsTab = SNew(SDockTab)
		[
			PreviewSettingsView.ToSharedRef()
		];

	if (TextureGraphPtr)
	{
		GetPreviewSettingsView()->SetObject(TextureGraphPtr->GetSettings(), true);
	}

	return SettingsTab.ToSharedRef();
}
TSharedRef<SDockTab> FTG_ExporterImpl::SpawnTab_ExportSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FTG_EditorTabs::OutputTabId);

	TSharedPtr<SDockTab> SettingsTab = SNew(SDockTab)
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.FillHeight(1)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SScrollBox)
						+SScrollBox::Slot()
						.VAlign(VAlign_Fill)
						.FillSize(1.0)
						[
							ExportSettingsView.ToSharedRef()
						]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SPrimaryButton)
				.Text(LOCTEXT("Export", "Export"))
				.OnClicked_Raw(this, &FTG_ExporterImpl::OnExportClicked, EAppReturnType::Ok)
			]
			
		];

	if (TextureGraphPtr)
	{
		GetExportSettingsView()->SetObject(ExportSettings, true);
	}

	return SettingsTab.ToSharedRef();
}
TSharedRef<SDockTab> FTG_ExporterImpl::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FTG_EditorTabs::ViewportTabId);

	TSharedRef< SDockTab > DockableTab =
		SNew(SDockTab);

	AssetEditorViewportFactoryFunction MakeViewportFunc = [this](const FAssetEditorViewportConstructionArgs& InArgs)
	{
		return SNew(STG_EditorViewport)
			.InTextureGraph(TextureGraphPtr);
	};

	// Create a new tab
	TSharedRef<FEditorViewportTabContent> ViewportTabContent = MakeShared<FEditorViewportTabContent>();
	//ViewportTabContent->OnViewportTabContentLayoutChanged().AddRaw(this, &FTG_ExporterImpl::OnEditorLayoutChanged);

	const FString LayoutId = FString("TG_EditorViewport");
	ViewportTabContent->Initialize(MakeViewportFunc, DockableTab, LayoutId);

	ViewportTabContentPtr = ViewportTabContent;
	
	// This call must occur after the toolbar is initialized.
	SetViewportPreviewMesh();
	
	return DockableTab;
}

TSharedRef<SDockTab> FTG_ExporterImpl::SpawnTab_ParameterDefaults(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FTG_EditorTabs::ParameterDefaultsTabId);

	return SNew(SDockTab)
		[
			SNew(SBox)
				[
					ParametersView.ToSharedRef()
				]
		];
}


TSharedRef<SDockTab> FTG_ExporterImpl::SpawnTab_NodePreview(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == FTG_EditorTabs::NodePreviewTabId);

	TSharedRef<STG_NodePreviewWidget> NodePreview = SNew(STG_NodePreviewWidget);
	NodePreviewPtr = NodePreview;
	
	return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(OutputNodesComboBoxWidget, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&OutputNodesList)
				.OnGenerateWidget_Raw(this, &FTG_ExporterImpl::GenerateOutputComboItem)
				.OnSelectionChanged_Raw(this, &FTG_ExporterImpl::OnOutputSelectionChanged)
				[
					SNew(STextBlock)
					.Text_Lambda([this] ()
					{
						FText ComboTitleText = FText::FromString(TEXT("No TextureGraph selected, or it has no Outputs")); 
						if (IsValid(SelectedNode))
						{
							ComboTitleText = FText::FromName(SelectedNode->GetExpression()->GetTitleName());
						}
						else if (!OutputNodesList.IsEmpty())
						{
							ComboTitleText = FText::FromName(*OutputNodesList[0]);
						}
						return ComboTitleText;
					})
				]
			]
			
			+ SVerticalBox::Slot()
			[
				NodePreview
			]
		];
}
void FTG_ExporterImpl::SetViewportPreviewMesh()
{
	if (TextureGraphPtr)
	{
		TObjectPtr<UStaticMesh> PreviewMesh = TextureGraphPtr->GetSettings()->GetPreviewMesh();
		// Set the preview mesh for the material.  
		if (!PreviewMesh || !SetPreviewAsset(PreviewMesh))
		{
			// The material preview mesh couldn't be found or isn't loaded. Fallback to the one of the primitive types.
			GetEditorViewport()->InitPreviewMesh();
		}
	}
}
TSharedPtr<STG_EditorViewport> FTG_ExporterImpl::GetEditorViewport() const
{
	if (ViewportTabContentPtr.IsValid())
	{
		// we can use static cast here b/c we know in this editor we will have a static mesh viewport 
		return StaticCastSharedPtr<STG_EditorViewport>(ViewportTabContentPtr.Pin()->GetFirstViewport());
	}

	return nullptr;
}
bool FTG_ExporterImpl::SetPreviewAsset(UObject* InAsset)
{
	if (GetEditorViewport().IsValid())
	{
		return GetEditorViewport()->SetPreviewAsset(InAsset);
	}
	return false;
}
// Function to generate combo box items
TSharedRef<SWidget> FTG_ExporterImpl::GenerateOutputComboItem(TSharedPtr<FName> InItem)
{
	return SNew(STextBlock).Text(FText::FromName(*InItem));
}

// Function called when the selection changes
void FTG_ExporterImpl::OnOutputSelectionChanged(TSharedPtr<FName> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectedItem.IsValid())
	{
		FName SelectedNodeName = *SelectedItem;
		FTG_Id SelectedNodeId = FTG_Id::INVALID;

		TextureGraphPtr->Graph()->ForEachNodes([this, &SelectedNodeId, SelectedNodeName](const UTG_Node* Node, uint32 Index)
		{
			if (Node && Node->GetExpression()->IsA(UTG_Expression_Output::StaticClass()))
			{
				// choose a default node
				if (!SelectedNodeId.IsValid())
					SelectedNodeId = Node->GetId();

				// check if this is our selected node
				if (Node->GetExpression()->GetTitleName() == SelectedNodeName)
				{
					SelectedNodeId = Node->GetId();
				}
			}
		});
		
		SelectedNode = TextureGraphPtr->Graph()->GetNode(SelectedNodeId);
		if (NodePreviewPtr.IsValid())
		{
			NodePreviewPtr.Pin()->SelectionChanged(SelectedNode);
		}
	
	}
}

FReply FTG_ExporterImpl::OnExportClicked(EAppReturnType::Type ButtonID)
{
	if (TextureGraphPtr)
	{
		FTG_HelperFunctions::ExportAsync(TextureGraphPtr, "", "", this->TargetExportSettings, false);
	}
	return FReply::Handled();
}
void FTG_ExporterImpl::SetTextureGraphToExport(UTextureGraph* InTextureGraph)
{
	// clear out previous handles
	Cleanup();

	// overwrite the original TextureGraph in place by constructing a new one with the same name
	FObjectDuplicationParameters Params = InitStaticDuplicateObjectParams(InTextureGraph, GetTransientPackage(), NAME_None,
	~RF_Standalone, UTextureGraph::StaticClass(), EDuplicateMode::Normal, EInternalObjectFlags::None);
	
	TextureGraphPtr = Cast<UTextureGraph>(StaticDuplicateObjectEx(Params));
	//Exporter gets notified when rendering is done
	TextureGraphPtr->OnRenderDone.BindRaw(this, &FTG_ExporterImpl::OnRenderingDone);

	// force open export window
	FGlobalTabmanager::Get()->TryInvokeTab(FTG_EditorTabs::TextureExporterTabId);
	
	
	UpdateParametersUI();

	UpdateExportSettingsUI();
	
	// Update list of output nodes in 2d View
	OutputNodesList.Empty();
	TextureGraphPtr->Graph()->ForEachNodes(
			[&](const UTG_Node* node, uint32 index)
	{
		if(UTG_Expression_Output* OutputExpression = Cast<UTG_Expression_Output>(node->GetExpression()))
		{
			OutputNodesList.Add(MakeShared<FName>(OutputExpression->GetTitleName()));			
		}
	});
	if (!OutputNodesList.IsEmpty())
	{
		OutputNodesComboBoxWidget->SetSelectedItem(OutputNodesList[0]);
	}
	OutputNodesComboBoxWidget->RefreshOptions();
	
	FViewportSettings& ViewportSettings = TextureGraphPtr->GetSettings()->GetViewportSettings();

	ViewportSettings.OnViewportMaterialChangedEvent.AddRaw(this, &FTG_ExporterImpl::OnViewportMaterialChanged);
	ViewportSettings.OnMaterialMappingChangedEvent.AddRaw(this, &FTG_ExporterImpl::OnMaterialMappingChanged);
	TextureGraphPtr->GetSettings()->OnPreviewMeshChangedEvent.AddRaw(this, &FTG_ExporterImpl::SetViewportPreviewMesh);
	TextureGraphPtr->Graph()->OnGraphChangedDelegate.AddRaw(this, &FTG_ExporterImpl::OnGraphChanged);
	GetPreviewSettingsView()->SetObject(TextureGraphPtr->GetSettings(), true);

	GetEditorViewport()->SetTextureGraph(TextureGraphPtr);
	OnViewportMaterialChanged();
	SetViewportPreviewMesh();
	
	TextureGraphEngine::RegisterErrorReporter(TextureGraphPtr, std::make_shared<FTextureGraphErrorReporter>());

}
void FTG_ExporterImpl::OnGraphChanged(UTG_Graph* InGraph, UTG_Node* InNode, bool Tweaking)
{
	if (TextureGraphPtr)
	{
		TextureGraphPtr->TriggerUpdate(Tweaking);
	
		// UpdateParametersUI();

		if (InNode->IsA<UTG_Expression_Output>())
		{
			TextureGraphPtr->UpdateGlobalTGSettings();
		}
		RefreshViewport();
	}
	// OutputView->ForceRefresh();
}

void FTG_ExporterImpl::OnRenderingDone(UMixInterface* TextureGraph, const FInvalidationDetails* Details)
{
	if (TextureGraph != nullptr && TextureGraph == TextureGraphPtr && NodePreviewPtr.IsValid())
	{
		// refresh node preview
		NodePreviewPtr.Pin()->Update();
	}
}
void FTG_ExporterImpl::OnViewportMaterialChanged()
{
	const UTG_Node* FirstTargetNode = nullptr;
	TextureGraphPtr->Graph()->ForEachNodes([&](const UTG_Node* node, uint32 index)
		{
			if (UTG_Expression_Output* OutputExpression = dynamic_cast<UTG_Expression_Output*>(node->GetExpression()))
			{
				if (!FirstTargetNode)
				{
					FirstTargetNode = node;
				}
			}
		});

	FViewportSettings& ViewportSettings = TextureGraphPtr->GetSettings()->GetViewportSettings();

	if (FirstTargetNode && ViewportSettings.MaterialMappingInfos.Num() > 0)
	{
		ViewportSettings.SetDefaultTarget(FirstTargetNode->GetNodeName());
	}

	GetEditorViewport()->GenerateRendermodeToolbar();
	GetEditorViewport()->InitRenderModes(TextureGraphPtr);
}

void FTG_ExporterImpl::OnMaterialMappingChanged()
{
	GetEditorViewport()->UpdateRenderMode();
}

void FTG_ExporterImpl::UpdateExportSettingsUI()
{
	// recreate export settings UI
	ExportSettings = NewObject<UTG_ExportSettings>(TextureGraphPtr);
	
	UTG_Graph* Graph = TextureGraphPtr->Graph();

	Graph->ForEachNodes([&](const UTG_Node* node, uint32 index)
	{
		if (UTG_Expression_Output* OutputExpression = dynamic_cast<UTG_Expression_Output*>(node->GetExpression()))
		{
			ExportSettings->OutputExpressionsInfos.Add(FOutputExpressionInfo{OutputExpression->GetTitleName(), node->GetId()});
		}
	});

	GetExportSettingsView()->SetObject(ExportSettings);
}
void FTG_ExporterImpl::UpdateParametersUI()
{
	UTG_Graph* Graph = TextureGraphPtr->Graph();
	FTG_Ids Ids = Graph->GetParamIds();

	// Create a new object to set for the view.
	Parameters = NewObject<UTG_Parameters>();

	for (const FTG_Id& id : Ids)
	{
		UTG_Pin* Pin = Graph->GetPin(id);

		if (Pin && (Pin->IsInput() || Pin->IsSetting()))
		{
			FTG_ParameterInfo Info{ id, Pin->GetAliasName() };
			Parameters->Parameters.Add(Info);
		}
	}
	Parameters->TextureGraph = Graph;

	GetParametersView()->SetObject(Parameters);
}
void FTG_ExporterImpl::Tick(float DeltaTime)
{
	RefreshViewport();
}

TStatId FTG_ExporterImpl::GetStatId() const
{
	return TStatId();
}

void FTG_ExporterImpl::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Parameters);
	Collector.AddReferencedObject(TextureGraphPtr);
	Collector.AddReferencedObject(ExportSettings);
}

void FTG_ExporterImpl::RefreshViewport()
{
	if (GetEditorViewport().IsValid())
	{
		GetEditorViewport()->RefreshViewport();
	}
}

///////////////////////////////////////////////////////////////////////////////////
FTG_Exporter::FTG_Exporter()
	: Impl(MakeUnique<FTG_ExporterImpl>())
{
}

FTG_Exporter::~FTG_Exporter()
{
}

void FTG_Exporter::SetTextureGraphToExport(UTextureGraph* InTextureGraph)
{
	Impl->SetTextureGraphToExport(InTextureGraph);
}

#undef LOCTEXT_NAMESPACE 
