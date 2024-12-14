// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Columns/TypedElementAlertColumns.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "Widgets/STedsTableViewer.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "QueryStack/FQueryStackNode_RowView.h"

#define LOCTEXT_NAMESPACE "TedsTableViewerModule"

static FName TableViewerTabName(TEXT("TedsTableViewer"));

class FTedsTableViewerModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		RegisterTableViewerTab();
	}

	virtual void ShutdownModule() override
	{
	}

	void RegisterTableViewerTab()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		
		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda([this]()
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");

			TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

			LevelEditorTabManager->RegisterTabSpawner(TableViewerTabName, FOnSpawnTab::CreateRaw(this, &FTedsTableViewerModule::OpenTableViewer))
			.SetDisplayName(LOCTEXT("TedsTableVIewerTitle", "Table Viewer (Experimental)"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorOutlinerCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"))
			.SetAutoGenerateMenuEntry(false); // This can only be summoned from the Cvar now
	
		});
	}
	
	
	TSharedRef<SDockTab> OpenTableViewer(const FSpawnTabArgs& SpawnTabArgs)
	{
		return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			CreateTableViewer()
		];
	}
	
	TSharedRef<SWidget> CreateTableViewer()
	{
		using namespace UE::Editor::DataStorage;

		if(!AreEditorDataStorageFeaturesEnabled())
		{
			return SNullWidget::NullWidget;
		}

		IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

		using namespace UE::Editor::DataStorage::Queries;

		// We'll just create a test table viewer that views all actor rows without actually updating it dynamically for now
		static QueryHandle QueryHandle =
			DataStorage->RegisterQuery(
				Select()
				.Where()
					.All<FTypedElementActorTag>()
				.Compile());

		Rows.Empty();
		
		FQueryResult QueryResult = DataStorage->RunQuery(QueryHandle,
			CreateDirectQueryCallbackBinding([this](const IEditorDataStorageProvider::IDirectQueryContext& Context, const RowHandle* RowHandles)
			{
				Rows.Append(RowHandles, Context.GetRowCount());
			}));

		return SNew(STedsTableViewer)
			.QueryStack(MakeShared<FQueryStackNode_RowView>(&Rows))
			.Columns({FTypedElementLabelColumn::StaticStruct(), FTypedElementSelectionColumn::StaticStruct(),
				FTypedElementAlertColumn::StaticStruct(), FTypedElementChildAlertColumn::StaticStruct()});
	}
	
	FDelegateHandle LevelEditorTabManagerChangedHandle;
	TArray<UE::Editor::DataStorage::RowHandle> Rows;
};

// CVar to summon a test table viewer that views a snapshot of all actors at the moment when the Cvar is used
static FAutoConsoleCommand OpenTableViewerConsoleCommand(
	TEXT("TEDS.UI.OpenTableViewer"),
	TEXT("Spawn the test TEDS Table Viewer."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");

		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if(TSharedPtr<SDockTab> DockTab = LevelEditorTabManager->FindExistingLiveTab(TableViewerTabName))
		{
			DockTab->RequestCloseTab();
		}

		LevelEditorTabManager->TryInvokeTab(TableViewerTabName);
	}));

IMPLEMENT_MODULE(FTedsTableViewerModule, TedsTableViewer);

#undef LOCTEXT_NAMESPACE //"TedsTableViewerModule"

