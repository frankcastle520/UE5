// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerModule.h"

#include "LevelEditor.h"
#include "SceneOutlinerPublicTypes.h"
#include "TedsOutlinerMode.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Elements/Columns/TypedElementAlertColumns.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Modules/ModuleManager.h"
#include "Compatibility/SceneOutlinerTedsBridge.h"
#include "Compatibility/SceneOutlinerRowHandleColumn.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "TedsOutlinerModule"

namespace UE::Editor::Outliner
{
	namespace Private
	{
		static bool bUseNewRevisionControlWidgets = false;
	} // namespace Private

	void RefreshLevelEditorTedsOutliner(bool bAlwaysInvoke)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		FTedsOutlinerModule& TedsOutlinerModule = FModuleManager::GetModuleChecked<FTedsOutlinerModule>("TedsOutliner");
		FName TabId = TedsOutlinerModule.GetTedsOutlinerTabName();

		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if(bAlwaysInvoke || LevelEditorTabManager->FindExistingLiveTab(TabId))
		{
			LevelEditorTabManager->TryInvokeTab(TabId);
		}
	}
	
	static FAutoConsoleVariableRef CVarUseNewRevisionControlWidgets(
		TEXT("TEDS.UI.UseNewRevisionControlWidgets"),
		Private::bUseNewRevisionControlWidgets,
		TEXT("Use new TEDS-based source control widgets in the Outliner (requires TEDS-Outliner to be enabled)")
		, FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			RefreshLevelEditorTedsOutliner(false);
		}));

	// CVar to summon the TEDS-Outliner as a separate tab
	static FAutoConsoleCommand OpenTableViewerConsoleCommand(
		TEXT("TEDS.UI.OpenTedsOutliner"),
		TEXT("Spawn the test TEDS-Outliner Integration."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			RefreshLevelEditorTedsOutliner(true);
		}));

FTedsOutlinerModule::FTedsOutlinerModule()
{
}

TSharedRef<ISceneOutliner> FTedsOutlinerModule::CreateTedsOutliner(const FSceneOutlinerInitializationOptions& InInitOptions, const FTedsOutlinerParams& InInitTedsOptions, DataStorage::QueryHandle ColumnQuery) const
{
	using namespace UE::Editor::DataStorage;
	ensureMsgf(AreEditorDataStorageFeaturesEnabled(), TEXT("Unable to initialize the Teds-Outliner before TEDS itself is initialized."));

	FSceneOutlinerInitializationOptions InitOptions(InInitOptions);
	FTedsOutlinerParams InitTedsOptions(InInitTedsOptions);

	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([&InitTedsOptions](SSceneOutliner* Outliner)
	{
		InitTedsOptions.SceneOutliner = Outliner;
		
		return new FTedsOutlinerMode(InitTedsOptions);
	});

	// Add the custom column that displays row handles
	if (InInitTedsOptions.bShowRowHandleColumn)
	{
		InitOptions.ColumnMap.Add(FSceneOutlinerRowHandleColumn::GetID(),
			FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 2,
				FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner)
				{
					return MakeShareable(new FSceneOutlinerRowHandleColumn(InSceneOutliner));
				})));
	}
	
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));

	
	TSharedRef<ISceneOutliner> TedsOutlinerShared = SNew(SSceneOutliner, InitOptions);
	
	FSceneOutlinerTedsQueryBinder::GetInstance().AssignQuery(ColumnQuery, TedsOutlinerShared, InitTedsOptions.CellWidgetPurposes);
	
	return TedsOutlinerShared;
}

void FTedsOutlinerModule::StartupModule()
{
	IModuleInterface::StartupModule();

	TedsOutlinerTabName = TEXT("LevelEditorTedsOutliner");
	RegisterLevelEditorTedsOutlinerTab();
}

void FTedsOutlinerModule::ShutdownModule()
{
	UnregisterLevelEditorTedsOutlinerTab();
	IModuleInterface::ShutdownModule();
}

DataStorage::QueryHandle FTedsOutlinerModule::GetLevelEditorTedsOutlinerColumnQuery()
{
	using namespace UE::Editor::DataStorage;
	IEditorDataStorageProvider* Storage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
		
	using namespace DataStorage::Queries;

	static DataStorage::QueryHandle ColumnQuery = Storage->RegisterQuery(
		Select()
			.ReadOnly<FTypedElementClassTypeInfoColumn, FTypedElementAlertColumn, FTypedElementChildAlertColumn>()
		.Compile());

	// Query to also include revision control info
	static DataStorage::QueryHandle RevisionControlQuery = Storage->RegisterQuery(
		Select()
			.ReadOnly<FTypedElementClassTypeInfoColumn, FTypedElementPackageReference, FTypedElementAlertColumn>()
		.Compile());

	return Private::bUseNewRevisionControlWidgets ? RevisionControlQuery : ColumnQuery;
	
}

TSharedRef<SWidget> FTedsOutlinerModule::CreateLevelEditorTedsOutliner()
{
	if(!DataStorage::AreEditorDataStorageFeaturesEnabled())
	{
		return SNew(STextBlock)
		.Text(LOCTEXT("TEDSPluginNotEnabledText", "You need to enable the Typed Element Data Storage plugin to see the table viewer!"));
	}

	using namespace DataStorage::Queries;

	// The Outliner is populated with Actors and Entities
	DataStorage::FQueryDescription OutlinerQueryDescription =
		Select()
		.Where()
			.All<FTypedElementClassTypeInfoColumn>() // TEDS-Outliner TODO: Currently looking at all entries with type info in TEDS
		.Compile();

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.FilterBarOptions.bHasFilterBar = true;
	InitOptions.bShowTransient = true;
	InitOptions.OutlinerIdentifier = "TEDSOutliner";

	FTedsOutlinerParams Params(nullptr);
	Params.QueryDescription = OutlinerQueryDescription;
	Params.bUseDefaultTedsFilters = true;

	// Example Query to filter for actors
	DataStorage::FQueryDescription ActorFilterQuery =
		Select()
		.Where()
			.All<FTypedElementActorTag>()
		.Compile();
	Params.FilterQueries.Emplace("Actors", ActorFilterQuery);
		
	// Empty selection set name is currently the level editor
	Params.SelectionSetOverride = FName();
	
	TSharedRef<ISceneOutliner> TEDSOutlinerShared = CreateTedsOutliner(InitOptions, Params, GetLevelEditorTedsOutlinerColumnQuery());
	
	return TEDSOutlinerShared;
}

TSharedRef<SDockTab> FTedsOutlinerModule::OpenLevelEditorTedsOutliner(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			CreateLevelEditorTedsOutliner()
		];
}

// The TEDS-Outliner as a separate tab
void FTedsOutlinerModule::RegisterLevelEditorTedsOutlinerTab()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		
	LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda([this]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");

		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		LevelEditorTabManager->RegisterTabSpawner(TedsOutlinerTabName, FOnSpawnTab::CreateRaw(this, &FTedsOutlinerModule::OpenLevelEditorTedsOutliner))
		.SetDisplayName(LOCTEXT("TedsTableVIewerTitle", "Table Viewer (Experimental)"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorOutlinerCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"))
		.SetAutoGenerateMenuEntry(false); // This can only be summoned from the Cvar now
	
	});
}

void FTedsOutlinerModule::UnregisterLevelEditorTedsOutlinerTab()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
}

FName FTedsOutlinerModule::GetTedsOutlinerTabName()
{
	return TedsOutlinerTabName;
}
} // namsepace UE::Editor::Outliner

IMPLEMENT_MODULE(UE::Editor::Outliner::FTedsOutlinerModule, TedsOutliner);

#undef LOCTEXT_NAMESPACE // TedsOutlinerModule
