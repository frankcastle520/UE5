// Copyright Epic Games, Inc. All Rights Reserved.

#include "STedsDebugger.h"

#include "SceneOutlinerPublicTypes.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Framework/Docking/LayoutService.h"
#include "Modules/ModuleManager.h"
#include "QueryEditor/TedsQueryEditor.h"
#include "TedsOutlinerModule.h"
#include "TedsOutlinerMode.h"

#define LOCTEXT_NAMESPACE "STedsDebugger"

namespace UE::Editor::DataStorage::Debug
{
	namespace Private
	{
		FName QueryEditorToolTabName = TEXT("TEDS Query Editor");
		FName ToolbarTabName = TEXT("TEDS Debugger Toolbar");
	}

STedsDebugger::~STedsDebugger()
{
	if (AreEditorDataStorageFeaturesEnabled())
	{
		GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName)->UnregisterQuery(TableViewerQuery);
	}
}

void STedsDebugger::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& ConstructUnderMajorTab, const TSharedPtr<SWindow>& ConstructUnderWindow)
{
	// Create the tab manager for our sub tabsa
	TabManager = FGlobalTabmanager::Get()->NewTabManager(ConstructUnderMajorTab);
	TabManager->SetAllowWindowMenuBar(true);

	// Register Tab Spawners
	RegisterTabSpawners();

	// Setup the default layout
	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("TedsDebuggerLayout_v0")
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->AddTab(Private::ToolbarTabName, ETabState::OpenedTab)
			->SetHideTabWell(true)
		)
		->Split
		(
			FTabManager::NewSplitter()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
					->AddTab(Private::QueryEditorToolTabName, ETabState::OpenedTab)
			)
		)
	);

	Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni, Layout);

	ChildSlot
	[
		TabManager->RestoreFrom(Layout, ConstructUnderWindow).ToSharedRef()
	];

	TabManager->SetOnPersistLayout(
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

	// create & initialize main menu
	FMenuBarBuilder MenuBarBuilder = FMenuBarBuilder(TSharedPtr<FUICommandList>());

	MenuBarBuilder.AddPullDownMenu(
		LOCTEXT("WindowMenuLabel", "Window"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &STedsDebugger::FillWindowMenu),
		"Window"
	);

	TSharedRef<SWidget> MenuWidget = MenuBarBuilder.MakeWidget();

	// Tell tab-manager about the multi-box for platforms with a global menu bar
	TabManager->SetMenuMultiBox(MenuBarBuilder.GetMultiBox(), MenuWidget);
}

void STedsDebugger::FillWindowMenu( FMenuBuilder& MenuBuilder)
{
	if (TabManager)
	{
		TabManager->PopulateLocalTabSpawnerMenu(MenuBuilder);
	}
}

TSharedRef<SDockTab> STedsDebugger::SpawnToolbar(const FSpawnTabArgs& Args)
{
	// The toolbar is currently empty but can be used to house tools that are not specific to a specific tab in the debugger
	FSlimHorizontalToolBarBuilder ToolBarBuilder(nullptr, FMultiBoxCustomization::None);

	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		.ShouldAutosize(true)
		[
			ToolBarBuilder.MakeWidget()
		];
}

TSharedRef<SDockTab> STedsDebugger::SpawnQueryEditorTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> DockTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	if (!QueryEditorModel)
	{
		if (AreEditorDataStorageFeaturesEnabled())
		{
			IEditorDataStorageProvider* DataStorageInterface = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
			QueryEditorModel = MakeUnique<QueryEditor::FTedsQueryEditorModel>(*DataStorageInterface);
		}
	}
	if (QueryEditorModel)
	{
		QueryEditorModel->Reset();	

		TSharedRef<QueryEditor::SQueryEditorWidget> QueryEditor =
			SNew(QueryEditor::SQueryEditorWidget, *QueryEditorModel);
		DockTab->SetContent(QueryEditor);
	}
	else
	{
		TSharedRef<STextBlock> TextBlock = SNew(STextBlock)
		.Text(LOCTEXT("TedsDebuggerModule_CannotLoadQueryEditor", "Cannot load Query Editor - Invalid Model"));
		DockTab->SetContent(TextBlock);
	}


	return DockTab;
}

void STedsDebugger::RegisterTabSpawners()
{
	const TSharedRef<FWorkspaceItem> AppMenuGroup =
		TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("TedsDebuggerGroupName", "Teds Debugger"));

	TabManager->RegisterTabSpawner(
		Private::ToolbarTabName,
		FOnSpawnTab::CreateRaw(this, &STedsDebugger::SpawnToolbar))
		.SetGroup(AppMenuGroup)
		.SetDisplayName(LOCTEXT("TedsDebugger_ToolbarDisplayName", "Toolbar"))
		.SetAutoGenerateMenuEntry(false);
	
	TabManager->RegisterTabSpawner(
		Private::QueryEditorToolTabName,
		FOnSpawnTab::CreateRaw(this, &STedsDebugger::SpawnQueryEditorTab))
		.SetGroup(AppMenuGroup)
		.SetDisplayName(LOCTEXT("TedsDebugger_QueryEditorDisplayName", "Query Editor"))
		.SetTooltipText(LOCTEXT("TedsDebugger_QueryEditorToolTip", "Opens the TEDS Query Editor"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Debug"));
}

} // namespace UE::Editor::DataStorage::Debug

#undef LOCTEXT_NAMESPACE
