// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "TedsOutlinerModule.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "SceneOutlinerPublicTypes.h"
#include "TedsOutlinerMode.h"
#include "ReferenceSkeleton.h"
#include "Animation/Skeleton.h"
#include "HierarchyTable/Columns/OverrideColumn.h"
#include "HierarchyTableType.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "HierarchyTableEditorModule.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "PersonaModule.h"
#include "TedsOutlinerItem.h"

#define LOCTEXT_NAMESPACE "HierarchyTableEditorToolkit"

void FHierarchyTableEditorToolkit::InitEditor(const TArray<UObject*>& InObjects)
{
	HierarchyTable = Cast<UHierarchyTable>(InObjects[0]);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("HierarchyTableEditorLayout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7f)
				->AddTab("HierarchyTableEditorTableTab", ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab("HierarchyTableEditorDetailsTab", ETabState::OpenedTab)
			)
		);

	FAssetEditorToolkit::InitAssetEditor(EToolkitMode::Standalone, {}, "HierarchyTableEditor", Layout, true, true, InObjects);

	ExtendToolbar();
}

void FHierarchyTableEditorToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("HierarchyTableEditor", "Hierarchy Table Editor"));

	InTabManager->RegisterTabSpawner("HierarchyTableEditorTableTab", FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				[
					CreateTedsOutliner()
				];
		}))
		.SetDisplayName(LOCTEXT("HierarchyTable", "Hierarchy Table"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());


	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObjects(TArray<UObject*>{ HierarchyTable });
	InTabManager->RegisterTabSpawner("HierarchyTableEditorDetailsTab", FOnSpawnTab::CreateLambda([=](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				[
					DetailsView
				];
		}))
		.SetDisplayName(INVTEXT("Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FHierarchyTableEditorToolkit::OnClose()
{
	using namespace UE::Editor::DataStorage;
	IEditorDataStorageProvider* DSI = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);

	for (const TTuple<int32, RowHandle>& Row : EntryIndexToHandleMap)
	{
		DSI->RemoveRow(Row.Value);
	}
}

void FHierarchyTableEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner("HierarchyTableEditorTableTab");
	InTabManager->UnregisterTabSpawner("HierarchyTableEditorDetailsTab");
}

TSharedRef<SWidget> FHierarchyTableEditorToolkit::CreateTedsOutliner()
{
	using namespace UE::Editor::DataStorage::Queries;
	using namespace UE::Editor::Outliner;

	if (!AreEditorDataStorageFeaturesEnabled())
	{
		return SNew(STextBlock)
			.Text(INVTEXT("You need to enable the Typed Element Data Storage plugin to see the table viewer!"));
	}

	if (!ensure(HierarchyTable->TableType))
	{
		return SNullWidget::NullWidget;
	}

	FHierarchyTableEditorModule& HierarchyTableModule = FModuleManager::GetModuleChecked<FHierarchyTableEditorModule>("HierarchyTableEditor");
	const UHierarchyTableTypeHandler_Base* Handler = HierarchyTableModule.FindHandler(HierarchyTable->TableType);
	if (!ensureMsgf(Handler, TEXT("Could not find handler for %s, have you forgotten to register it?"), *HierarchyTable->TableType->GetName()))
	{
		return SNullWidget::NullWidget;
	}

	TArray<UScriptStruct*> HierarchyTableTypeColumns = Handler->GetColumns();
	HierarchyTableTypeColumns.Add(FTypedElementOverrideColumn::StaticStruct());

	FQueryDescription ColumnQueryDescription =
		Select()
			.ReadOnly(HierarchyTableTypeColumns)
		.Compile();

	InitialColumnQuery = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName)->RegisterQuery(MoveTemp(ColumnQueryDescription));

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.FilterBarOptions.bHasFilterBar = true;
	InitOptions.OutlinerIdentifier = "HierarchyTableTedsOutliner";

	FTedsOutlinerParams Params(nullptr);
	{
		FQueryDescription RowQueryDescription =
			Select()
			.Where()
			.All<FTypedElementOverrideColumn>()
			.Compile();

		Params.QueryDescription = RowQueryDescription;
		Params.CellWidgetPurposes = TArray<FName>{ TEXT("General.Cell") };
		Params.HierarchyData = FTedsOutlinerHierarchyData::GetDefaultHierarchyData();
		Params.bShowRowHandleColumn = false;
	}

	FTedsOutlinerModule& TedsOutlinerModule = FModuleManager::GetModuleChecked<FTedsOutlinerModule>("TedsOutliner");

	IEditorDataStorageProvider* DSI = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	static TableHandle Table = DSI->FindTable(FName("Editor_HierarchyTableTable"));
	
	TArray<UScriptStruct*> BaseHierarchyTableTypeColumns = Handler->GetColumns();

	for (int32 EntryIndex = 0; EntryIndex < HierarchyTable->TableData.Num(); ++EntryIndex)
	{
		FHierarchyTableEntryData* Entry = &HierarchyTable->TableData[EntryIndex];

		RowHandle Row = DSI->AddRow(Table);

		FTypedElementOverrideColumn OverrideEntry;
		OverrideEntry.OwnerEntryIndex = EntryIndex;
		OverrideEntry.OwnerTable = HierarchyTable;
		DSI->AddColumn(Row, MoveTemp(OverrideEntry));

		DSI->AddColumn<FTypedElementLabelColumn>(Row, { .Label = Entry->Identifier.ToString() });

		RowHandle* ParentRow = EntryIndexToHandleMap.Find(Entry->Parent);
		if (ParentRow)
		{
			DSI->AddColumn<FTableRowParentColumn>(Row, { .Parent = *ParentRow });
		}

		for (const UScriptStruct* Column : BaseHierarchyTableTypeColumns)
		{
			DSI->AddColumn(Row, Column);
		}

		EntryIndexToHandleMap.Add(EntryIndex, Row);
	}

	TSharedRef<ISceneOutliner> TedsOutliner = TedsOutlinerModule.CreateTedsOutliner(InitOptions, Params, InitialColumnQuery);
	TedsOutlinerPtr = TedsOutliner;

	return TedsOutliner;
}

void FHierarchyTableEditorToolkit::ExtendToolbar()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	FName ParentName;
	static const FName MenuName = GetToolMenuToolbarName(ParentName);

	UToolMenu* ToolMenu = UToolMenus::Get()->ExtendMenu(MenuName);
	const FToolMenuInsert SectionInsertLocation("Asset", EToolMenuInsertType::After);
	{
		FToolMenuSection& HierarchyTableSection = ToolMenu->AddSection("HierarchyTable", LOCTEXT("HierarchyTable_ToolbarLabel", "HierarchyTable"), SectionInsertLocation);

		HierarchyTableSection.AddEntry(FToolMenuEntry::InitComboButton(
			"AddCurve",
			FUIAction(),
			FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InSubMenu)
				{
					FToolUIAction Action;
				    Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this](const FToolMenuContext& Context)
				    {
						TSharedRef<STextEntryPopup> TextEntry = SNew(STextEntryPopup)
							.Label(LOCTEXT("NewCurveEntryLabal", "Curve Name"))
							.OnTextCommitted_Lambda([this](const FText& CommittedText, ETextCommit::Type CommitInfo)
								{
									AddEntry(FName(CommittedText.ToString()), EHierarchyTableEntryType::Curve);
									FSlateApplication::Get().DismissAllMenus();
								});

						FSlateApplication& SlateApp = FSlateApplication::Get();
						SlateApp.PushMenu(
							SlateApp.GetInteractiveTopLevelWindows()[0],
							FWidgetPath(),
							TextEntry,
							SlateApp.GetCursorPos(),
							FPopupTransitionEffect::TypeInPopup);
				    });

				    FToolMenuEntry Entry = FToolMenuEntry::InitMenuEntry(
					    FName("AddNewCurve"),
					    LOCTEXT("AddNewCurve_Label", "Add New Curve"),
					    LOCTEXT("AddNewCurve_Tooltip", "Add a new curve value"),
					    FSlateIcon(),
					    FToolUIActionChoice(Action),
						EUserInterfaceActionType::Button);
				
				    InSubMenu->AddMenuEntry("AddNewCurve", Entry);

					FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");

					InSubMenu->AddMenuEntry("ExistingCurveMenu", FToolMenuEntry::InitWidget(
						"ExistingCurveMenu",
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.AutoHeight()
						[
							PersonaModule.CreateCurvePicker(HierarchyTable->Skeleton,
								FOnCurvePicked::CreateLambda([this](const FName& InName)
									{
										AddEntry(InName, EHierarchyTableEntryType::Curve);
										FSlateApplication::Get().DismissAllMenus();
									}))
						],
						FText(),
						true,
						false,
						true
					));
				}),
			LOCTEXT("AddCurve_Label", "Add Curve"),
			LOCTEXT("AddCurve_ToolTip", "Add a new curve to the hierarchy"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Plus")
		));

		HierarchyTableSection.AddEntry(FToolMenuEntry::InitComboButton(
			"AddAttribute",
			FUIAction(),
			FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InSubMenu)
				{
					FToolUIAction Action;
				    Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this](const FToolMenuContext& Context)
				    {
						TSharedRef<STextEntryPopup> TextEntry = SNew(STextEntryPopup)
							.Label(LOCTEXT("NewAttributeEntryLabal", "Attribute Name"))
							.OnTextCommitted_Lambda([this](const FText& CommittedText, ETextCommit::Type CommitInfo)
								{
									AddEntry(FName(CommittedText.ToString()), EHierarchyTableEntryType::Attribute);
									FSlateApplication::Get().DismissAllMenus();
								});

						FSlateApplication& SlateApp = FSlateApplication::Get();
						SlateApp.PushMenu(
							SlateApp.GetInteractiveTopLevelWindows()[0],
							FWidgetPath(),
							TextEntry,
							SlateApp.GetCursorPos(),
							FPopupTransitionEffect::TypeInPopup);
				    });

				    FToolMenuEntry Entry = FToolMenuEntry::InitMenuEntry(
					    FName("AddNewAttribute"),
					    LOCTEXT("AddNewAttribute_Label", "Add New Attribute"),
					    LOCTEXT("AddNewAttribute_Tooltip", "Add a new attribute value"),
					    FSlateIcon(),
					    FToolUIActionChoice(Action),
						EUserInterfaceActionType::Button);
				
				    InSubMenu->AddMenuEntry("AddNewAttribute", Entry);
				}),
			LOCTEXT("AddAttribute_Label", "Add Attribute"),
			LOCTEXT("AddAttribute_ToolTip", "Add a new attribute to the hierarchy"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Plus")
		));
	}
}

void FHierarchyTableEditorToolkit::AddEntry(const FName Identifier, const EHierarchyTableEntryType EntryType)
{
	check(EntryType != EHierarchyTableEntryType::Bone);

	if (HierarchyTable->HasIdentifier(Identifier))
	{
		// Avoid adding duplicate entries
		return;
	}

	using namespace UE::Editor::DataStorage;
	IEditorDataStorageProvider* DSI = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
	static TableHandle Table = DSI->FindTable(FName("Editor_HierarchyTableTable"));

	int32 ParentIndex = 0; // root of hierarchy

	TArray<FSceneOutlinerTreeItemPtr> Selection = TedsOutlinerPtr->GetTree().GetSelectedItems();
	if (Selection.Num() > 0)
	{
		const UE::Editor::Outliner::FTedsOutlinerTreeItem* TedsItem = Selection[0]->CastTo<UE::Editor::Outliner::FTedsOutlinerTreeItem>();
		const UE::Editor::DataStorage::RowHandle ParentRowHandle = TedsItem->GetRowHandle();

		const FTypedElementOverrideColumn* OverrideColumn = DSI->GetColumn<FTypedElementOverrideColumn>(ParentRowHandle);
		ParentIndex = OverrideColumn->OwnerEntryIndex;
	}

	FHierarchyTableEntryData EntryData;
	{
		EntryData.Identifier = Identifier;
		EntryData.EntryType = EntryType;
		EntryData.OwnerTable = HierarchyTable;
		EntryData.Parent = ParentIndex;
		EntryData.Payload = TOptional<FInstancedStruct>();
	}
	const int32 EntryIndex = HierarchyTable->TableData.Add(EntryData);

	const RowHandle Row = DSI->AddRow(Table);

	{
		FTypedElementOverrideColumn OverrideEntry;
		OverrideEntry.OwnerEntryIndex = EntryIndex;
		OverrideEntry.OwnerTable = HierarchyTable;
		DSI->AddColumn(Row, MoveTemp(OverrideEntry));
	}

	// Ideally would read the label directly from the HT entry struct instead of storing it itself
	// but this is a built-in TEDS column type that is used the the tabel viewer widget.
	DSI->AddColumn<FTypedElementLabelColumn>(Row, { .Label = EntryData.Identifier.ToString() });

	const RowHandle* ParentRow = EntryIndexToHandleMap.Find(EntryData.Parent);
	if (ParentRow)
	{
		DSI->AddColumn<FTableRowParentColumn>(Row, { .Parent = *ParentRow });
	}

	FHierarchyTableEditorModule& HierarchyTableModule = FModuleManager::GetModuleChecked<FHierarchyTableEditorModule>("HierarchyTableEditor");
	const UHierarchyTableTypeHandler_Base* Handler = HierarchyTableModule.FindHandler(HierarchyTable->TableType);

	for (const UScriptStruct* Column : Handler->GetColumns())
	{
		DSI->AddColumn(Row, Column);
	}

	EntryIndexToHandleMap.Add(EntryIndex, Row);
}

#undef LOCTEXT_NAMESPACE