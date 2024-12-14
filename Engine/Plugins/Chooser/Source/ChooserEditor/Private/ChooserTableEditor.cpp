// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChooserTableEditor.h"

#include "Chooser.h"
#include "ChooserDetails.h"
#include "ChooserEditorWidgets.h"
#include "ChooserFindProperties.h"
#include "ChooserTableEditorCommands.h"
#include "ClassViewerFilter.h"
#include "DetailCategoryBuilder.h"
#include "Factories.h"
#include "GraphEditorSettings.h"
#include "IDetailsView.h"
#include "IPropertyAccessEditor.h"
#include "LandscapeRender.h"
#include "ObjectChooserClassFilter.h"
#include "ObjectChooserWidgetFactories.h"
#include "ObjectChooser_Asset.h"
#include "ObjectChooser_Class.h"
#include "PersonaModule.h"
#include "StructUtils/PropertyBag.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "RandomizeColumn.h"
#include "SAssetDropTarget.h"
#include "SChooserColumnHandle.h"
#include "SClassViewer.h"
#include "ScopedTransaction.h"
#include "SNestedChooserTree.h"
#include "SourceCodeNavigation.h"
#include "StructViewerModule.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/TransactionObjectEvent.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "SChooserTableRow.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Views/SListView.h"
#include "ToolMenus.h"
#include "UnrealExporter.h"
#include "Exporters/Exporter.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Commands/GenericCommands.h"
#include "SPositiveActionButton.h"

#define LOCTEXT_NAMESPACE "ChooserEditor"

namespace UE::ChooserEditor
{

const FName FChooserTableEditor::ToolkitFName( TEXT( "ChooserTableEditor" ) );
const FName FChooserTableEditor::PropertiesTabId( TEXT( "ChooserEditor_Properties" ) );
const FName FChooserTableEditor::FindReplaceTabId( TEXT( "ChooserEditor_FindReplace" ) );
const FName FChooserTableEditor::TableTabId( TEXT( "ChooserEditor_Table" ) );
const FName FChooserTableEditor::NestedTablesTreeTabId( TEXT( "ChooserEditor_NestedTables" ) );

constexpr int HistorySize = 16;	

void FChooserTableEditor::AddHistory()
{
	// remove anything ahead of this in the history, if we had gone back
	while (HistoryIndex !=0)
	{
		History.PopFront();
		HistoryIndex--;
	}
	
	if (History.Num() >= HistorySize)
	{
		History.Pop();
	}
	History.AddFront(GetChooser());
}

bool FChooserTableEditor::CanNavigateBack() const
{
	return HistoryIndex < History.Num() - 1;
}

void FChooserTableEditor::NavigateBack()
{
	if (HistoryIndex < History.Num() - 1)
	{
		HistoryIndex++;
		SetChooserTableToEdit(History[HistoryIndex], false);
	}
}

bool FChooserTableEditor::CanNavigateForward() const
{
	return HistoryIndex > 0;
}

void FChooserTableEditor::NavigateForward()
{
	if (HistoryIndex > 0)
	{
		HistoryIndex--;
		SetChooserTableToEdit(History[HistoryIndex], false);
	}
}

void FChooserTableEditor::SetChooserTableToEdit(UChooserTable* Chooser, bool bApplyToHistory)
{
	if (Chooser == GetChooser())
	{
		return;
	}
	
	BreadcrumbTrail->ClearCrumbs();

	TArray<UChooserTable*> OuterList;
	OuterList.Push(Chooser);
	
	while(OuterList.Last() != GetRootChooser())
	{
		OuterList.Push(Cast<UChooserTable>(OuterList.Last()->GetOuter()));
	}

	while(!OuterList.IsEmpty())
	{
		UChooserTable* Popped = OuterList.Pop();
		BreadcrumbTrail->PushCrumb(FText::FromString(Popped->GetName()), Popped);
	}
	
	if (bApplyToHistory)
	{
		AddHistory();
	}
	
	RefreshAll();
}

void FChooserTableEditor::PushChooserTableToEdit(UChooserTable* Chooser)
{
	BreadcrumbTrail->PushCrumb(FText::FromString(Chooser->GetName()), Chooser);
	AddHistory();
	RefreshAll();
}
	
void FChooserTableEditor::PopChooserTableToEdit()
{
	if (BreadcrumbTrail->HasCrumbs())
	{
		BreadcrumbTrail->PopCrumb();
		RefreshAll();
	}
}
	
void FChooserTableEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_ChooserTableEditor", "Chooser Table Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner( PropertiesTabId, FOnSpawnTab::CreateSP(this, &FChooserTableEditor::SpawnPropertiesTab) )
		.SetDisplayName( LOCTEXT("PropertiesTab", "Details") )
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon("EditorStyle", "LevelEditor.Tabs.Details"));
		
	InTabManager->RegisterTabSpawner( TableTabId, FOnSpawnTab::CreateSP(this, &FChooserTableEditor::SpawnTableTab) )
		.SetDisplayName( LOCTEXT("TableTab", "Chooser Table") )
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon("ChooserEditorStyle", "ChooserEditor.ChooserTableIconSmall"));
		
	InTabManager->RegisterTabSpawner( NestedTablesTreeTabId, FOnSpawnTab::CreateSP(this, &FChooserTableEditor::SpawnNestedTablesTreeTab) )
		.SetDisplayName( LOCTEXT("NestedTablesTab", "Nested Choosers") )
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon("ChooserEditorStyle", "ChooserEditor.ChooserTableIconSmall"));


	InTabManager->RegisterTabSpawner( FindReplaceTabId, FOnSpawnTab::CreateSP(this, &FChooserTableEditor::SpawnFindReplaceTab) )
		.SetDisplayName( LOCTEXT("FindReplaceTab", "Find/Replace") )
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Find"));
}
	
void FChooserTableEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner( TableTabId );
	InTabManager->UnregisterTabSpawner( PropertiesTabId );
	InTabManager->UnregisterTabSpawner( FindReplaceTabId );
}

const FName FChooserTableEditor::ChooserEditorAppIdentifier( TEXT( "ChooserEditorApp" ) );

FChooserTableEditor::~FChooserTableEditor()
{
	if (SelectedColumn)
	{
		SelectedColumn->RemoveFromRoot();
		SelectedColumn = nullptr;
	}
	for (UObject* SelectedRow : SelectedRows)
	{
		SelectedRow->ClearFlags(RF_Standalone);
	}
	SelectedRows.Empty();
	
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectTransacted.RemoveAll(this);

	DetailsView.Reset();
}

FName FChooserTableEditor::EditorName = "ChooserTableEditor";
	
FName FChooserTableEditor::ContextMenuName("ChooserEditorContextMenu"); // todo: for this to actually be extensible this needs to be somewhere public
	
FName FChooserTableEditor::GetEditorName() const
{
	return EditorName;
}

void FChooserTableEditor::MakeDebugTargetMenu(UToolMenu* InToolMenu) 
{
	static FName SectionName = "Select Debug Target";
	InToolMenu->bSearchable = true;
		
	InToolMenu->AddMenuEntry(
			SectionName,
			FToolMenuEntry::InitMenuEntry(
				"None",
				LOCTEXT("None", "None"),
				LOCTEXT("None Tooltip", "Clear selected debug target"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this]()
					{
						UChooserTable* Chooser = GetRootChooser();
						Chooser->ResetDebugTarget();
						if (Chooser->GetEnableDebugTesting())
						{
							Chooser->SetEnableDebugTesting(false);
							Chooser->SetDebugTestValuesValid(false);
							UpdateTableColumns();
						}
					}),
					FCanExecuteAction()
				)
			));
	
	InToolMenu->AddMenuEntry(
			SectionName,
			FToolMenuEntry::InitMenuEntry(
				"Manual",
				LOCTEXT("Manual Testing", "Manual Testing"),
				LOCTEXT("Manual Tooltip", "Test the chooser by manually entering values for each column"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this]()
					{
						UChooserTable* Chooser = GetRootChooser();
						Chooser->ResetDebugTarget();
						if (!Chooser->GetEnableDebugTesting())
						{
							Chooser->SetEnableDebugTesting(true);
							Chooser->SetDebugTestValuesValid(true);
							UpdateTableColumns();
						}
					}),
					FCanExecuteAction()
				)
			));

	const UChooserTable* Chooser = GetChooser();

	Chooser->IterateRecentContextObjects([this, InToolMenu](const FString& ObjectName)
		{
			InToolMenu->AddMenuEntry(
						SectionName,
						FToolMenuEntry::InitMenuEntry(
							FName(ObjectName),
							FText::FromString(ObjectName),
							LOCTEXT("Select Object ToolTip", "Select this object as the debug target"),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([this, ObjectName]()
								{
									UChooserTable* Chooser = GetRootChooser();
									Chooser->SetDebugTarget(ObjectName);
									Chooser->SetDebugTestValuesValid(false);
									if (!Chooser->GetEnableDebugTesting())
									{
										Chooser->SetEnableDebugTesting(true);
										UpdateTableColumns();
									}
								}),
								FCanExecuteAction()
							)
						));
		}
	);

}
	
TSharedPtr<SWidget> FChooserTableEditor::GenerateRowContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	FToolMenuContext ToolMenuContext;
	InitToolMenuContext(ToolMenuContext);
	return ToolMenus->GenerateWidget(ContextMenuName, ToolMenuContext);
}

void FChooserTableEditor::RegisterToolbar()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* ToolBar;
	FName ParentName;
	const FName MenuName = GetToolMenuToolbarName(ParentName);
	if (ToolMenus->IsMenuRegistered(MenuName))
	{
		ToolBar = ToolMenus->ExtendMenu(MenuName);
	}
	else
	{
		ToolBar = UToolMenus::Get()->RegisterMenu(MenuName, ParentName, EMultiBoxType::ToolBar);
	}

	const FChooserTableEditorCommands& Commands = FChooserTableEditorCommands::Get();
	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		FToolMenuSection& Section = ToolBar->AddSection("Chooser", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.EditChooserSettings,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon("EditorStyle", "FullBlueprintEditor.EditGlobalOptions")));

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.AutoPopulateAll));


		Section.AddDynamicEntry("DebuggingCommands", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UChooserEditorToolMenuContext* Context = InSection.FindContext<UChooserEditorToolMenuContext>();

			if (Context)
			{
				if (TSharedPtr<FChooserTableEditor> ChooserEditor = Context->ChooserEditor.Pin())
				{
					InSection.AddEntry(FToolMenuEntry::InitComboButton( "SelectDebugTarget",
						FToolUIActionChoice(),
					  FNewToolMenuDelegate::CreateSP(ChooserEditor.Get(), &FChooserTableEditor::MakeDebugTargetMenu),
						TAttribute<FText>::CreateLambda([Chooser = ChooserEditor->GetRootChooser() ]
						{
							if (Chooser->HasDebugTarget())
							{
								return  FText::FromString(Chooser->GetDebugTargetName());
							}
							else
							{
								return Chooser->GetEnableDebugTesting() ? LOCTEXT("Manual Testing", "Manual Testing") : LOCTEXT("Debug Target", "Debug Target");
							}
						}),
						LOCTEXT("Debug Target Tooltip", "Select an object that has recently been the context object for this chooser to visualize the selection results")));
				}
			}
		}));
	}

}

void FChooserTableEditor::RegisterMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	const FChooserTableEditorCommands& Commands = FChooserTableEditorCommands::Get();

	// Table Context Menu
	UToolMenu* ToolMenu;
	if (ToolMenus->IsMenuRegistered(ContextMenuName))
	{
		ToolMenu = ToolMenus->ExtendMenu(ContextMenuName);
	}
	else
	{
		ToolMenu = UToolMenus::Get()->RegisterMenu(ContextMenuName, NAME_None, EMultiBoxType::Menu);
	}

	if (ToolMenu)
	{
		FToolMenuSection& Section = ToolMenu->AddSection("ChooserEditorContext", TAttribute<FText>());
	
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(FGenericCommands::Get().Copy));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(FGenericCommands::Get().Cut));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(FGenericCommands::Get().Paste));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(FGenericCommands::Get().Duplicate));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(FGenericCommands::Get().Delete));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(Commands.Disable));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(Commands.MoveUp));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(Commands.MoveDown));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(Commands.MoveLeft));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(Commands.MoveRight));
		Section.AddEntry(FToolMenuEntry::InitMenuEntry(Commands.AutoPopulateSelection));
		

		Section.AddDynamicEntry("ColumnInputType", FNewToolMenuSectionDelegate::CreateLambda([this] (FToolMenuSection& Section)
		{
			if (CurrentSelectionType == ESelectionType::Column && SelectedColumn)
			{
				Section.AddSubMenu("ParameterType", LOCTEXT("Parameter Type", "Parameter Type"),
					LOCTEXT("Parameter Type Tooltip", "Change the type of input/output parameter for this column"),
					FNewToolMenuChoice(FOnGetContent::CreateLambda([this]()
					{
						int ColumnIndex = SelectedColumn->Column;
						UChooserTable* Chooser = SelectedColumn->Chooser;
						FStructViewerInitializationOptions Options;
						Options.StructFilter = MakeShared<FStructFilter>(Chooser->ColumnsStructs[ColumnIndex].Get<FChooserColumnBase>().GetInputBaseType());
						Options.bAllowViewOptions = false;
						Options.bShowNoneOption = false;
						Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
					
						// Add class filter for columns here
						TSharedRef<SWidget> Widget = FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer").CreateStructViewer(Options, FOnStructPicked::CreateLambda([this, ColumnIndex](const UScriptStruct* ChosenStruct)
						{
							const FScopedTransaction Transaction(LOCTEXT("SetColumnInputType", "Set Column Input Type"));
							UChooserTable* ChooserTable = GetChooser();
							ChooserTable->ColumnsStructs[ColumnIndex].GetMutable<FChooserColumnBase>().SetInputType(ChosenStruct);
							ChooserTable->Modify(true);
							UpdateTableColumns();
							UpdateTableRows();
							
							if (SelectedColumn && SelectedColumn->Column == ColumnIndex)
							{
								// if this column was selected, reselect to refresh the details widgets
								SelectColumn(ChooserTable, ColumnIndex);
							}
														
						}));
				
						return Widget;
					}))
					);
			}
		}));
	}

	
	struct Local
	{
		static void FillEditMenu(FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.BeginSection("ChooserEditing", LOCTEXT("Chooser Table Editing", "Chooser Table"));
			{
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy, NAME_None);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut, NAME_None);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste, NAME_None);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate, NAME_None, LOCTEXT("Duplicate Selection", "Duplicate Selection"));
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, LOCTEXT("Delete Selection", "Delete Selection"));
				MenuBuilder.AddMenuEntry(FChooserTableEditorCommands::Get().Disable, NAME_None, LOCTEXT("Disable Selection", "Disable Selection"));
				MenuBuilder.AddMenuEntry(FChooserTableEditorCommands::Get().RemoveDisabledData, NAME_None);
			}
			MenuBuilder.EndSection();
		}
	};

	TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender);

	// Extend the Edit menu
	MenuExtender->AddMenuExtension(
		"EditHistory",
		EExtensionHook::After,
		GetToolkitCommands(),
		FMenuExtensionDelegate::CreateStatic(&Local::FillEditMenu));

	AddMenuExtender(MenuExtender);
}

void FChooserTableEditor::InitToolMenuContext(FToolMenuContext& MenuContext)
{
	FAssetEditorToolkit::InitToolMenuContext(MenuContext);

	UChooserEditorToolMenuContext* Context = NewObject<UChooserEditorToolMenuContext>();
	Context->ChooserEditor = SharedThis(this);
	MenuContext.AppendCommandList(GetToolkitCommands());
	MenuContext.AddObject(Context);
}


void FChooserTableEditor::BindCommands()
{
	const FChooserTableEditorCommands& Commands = FChooserTableEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.EditChooserSettings,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::SelectRootProperties));
	
	ToolkitCommands->MapAction(
		Commands.AutoPopulateAll,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::AutoPopulateAll));
	
	ToolkitCommands->MapAction(
		Commands.RemoveDisabledData,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::RemoveDisabledData));
	
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::DeleteSelection),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::HasSelection)
		);

	ToolkitCommands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::DuplicateSelection),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::HasSelection)
		);

	ToolkitCommands->MapAction(
		Commands.AutoPopulateSelection,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::AutoPopulateSelection),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::CanAutoPopulateSelection),
		FGetActionCheckState(),FIsActionButtonVisible::CreateSP(this, &FChooserTableEditor::HasSelection)
		);
	
	ToolkitCommands->MapAction(
		Commands.Disable,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::ToggleDisableSelection),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::HasSelection),
		FIsActionChecked::CreateSP(this, &FChooserTableEditor::IsSelectionDisabled),
		FIsActionButtonVisible::CreateSP(this, &FChooserTableEditor::HasSelection)
		);
	
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::CopySelection),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::HasSelection));
	
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::CutSelection),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::HasSelection));
	
	ToolkitCommands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::Paste),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::CanPaste));
	
	ToolkitCommands->MapAction(
		Commands.MoveUp,
		FExecuteAction::CreateSP(this, &FChooserTableEditor::MoveRowsUp),
		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::CanMoveRowsUp),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FChooserTableEditor::HasRowsSelected)
		);
	ToolkitCommands->MapAction(
    		Commands.MoveDown,
    		FExecuteAction::CreateSP(this, &FChooserTableEditor::MoveRowsDown),
    		FCanExecuteAction::CreateSP(this, &FChooserTableEditor::CanMoveRowsDown),
    		FIsActionChecked(),
    		FIsActionButtonVisible::CreateSP(this, &FChooserTableEditor::HasRowsSelected)
    		);
	
	ToolkitCommands->MapAction(
			Commands.MoveLeft,
			FExecuteAction::CreateSP(this, &FChooserTableEditor::MoveColumnLeft),
			FCanExecuteAction::CreateSP(this, &FChooserTableEditor::CanMoveColumnLeft),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateSP(this, &FChooserTableEditor::HasColumnSelected)
			);

	ToolkitCommands->MapAction(
			Commands.MoveRight,
			FExecuteAction::CreateSP(this, &FChooserTableEditor::MoveColumnRight),
			FCanExecuteAction::CreateSP(this, &FChooserTableEditor::CanMoveColumnRight),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateSP(this, &FChooserTableEditor::HasColumnSelected)
			);
}

void FChooserTableEditor::OnObjectsTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (UChooserTable* ChooserTable = Cast<UChooserTable>(Object))
	{
		// if this is the chooser we're editing
		if (GetChooser() == ChooserTable)
		{
			if (CurrentSelectionType == ESelectionType::Rows)
			{
				// refresh details if we have rows selected
				RefreshRowSelectionDetails();
			}
		}
	}
	
	if (UChooserRowDetails* RowDetails = Cast<UChooserRowDetails>(Object))
	{
		// if this is for the chooser we're editing
		if (GetChooser() == RowDetails->Chooser)
		{
			if (RowDetails->Chooser->ResultsStructs.IsValidIndex(RowDetails->Row))
			{
				// copy all the values over
				TValueOrError<FStructView, EPropertyBagResult> Result = RowDetails->Properties.GetValueStruct("Result", FInstancedStruct::StaticStruct());
				if (Result.IsValid())
				{
					RowDetails->Chooser->ResultsStructs[RowDetails->Row] = Result.GetValue().Get<FInstancedStruct>();
				}

				int ColumnIndex = 0;
				for (FInstancedStruct& ColumnData : RowDetails->Chooser->ColumnsStructs)
				{
					FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
					Column.SetFromDetails(RowDetails->Properties, ColumnIndex, RowDetails->Row);
					ColumnIndex++;
				}
			
				TValueOrError<bool, EPropertyBagResult> DisabledResult = RowDetails->Properties.GetValueBool("Disabled");
				RowDetails->Chooser->DisabledRows[RowDetails->Row] = DisabledResult.GetValue();
			}
			else if (RowDetails->Row == ColumnWidget_SpecialIndex_Fallback)
			{
				TValueOrError<FStructView, EPropertyBagResult> Result = RowDetails->Properties.GetValueStruct("Result", FInstancedStruct::StaticStruct());
				if (Result.IsValid())
				{
					RowDetails->Chooser->FallbackResult = Result.GetValue().Get<FInstancedStruct>();
				}
				
				int ColumnIndex = 0;
				for (FInstancedStruct& ColumnData : RowDetails->Chooser->ColumnsStructs)
				{
					FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
					Column.SetFromDetails(RowDetails->Properties, ColumnIndex, RowDetails->Row);
					ColumnIndex++;
				}
			}

			RefreshAll();
		}
	}
}

void FChooserTableEditor::InitEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects )
{
	UChooserTable* Chooser = Cast<UChooserTable>(ObjectsToEdit[0]);
	RootChooser = Chooser->GetRootChooser();
	check(RootChooser);

	History.Reserve(HistorySize);
	BreadcrumbTrail = SNew(SBreadcrumbTrail<UChooserTable*>)
		.ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
		.TextStyle(FAppStyle::Get(), "GraphBreadcrumbButtonText")
		.ButtonContentPadding( FMargin(4.f, 2.f) )
		.DelimiterImage( FAppStyle::GetBrush("BreadcrumbTrail.Delimiter") )
		.OnCrumbPushed_Lambda([this](UChooserTable* Table)
		{
			RefreshAll();
		})
		.OnCrumbClicked_Lambda([this](UChooserTable* Table)
		{
			AddHistory();
			RefreshAll();
		})
		.GetCrumbMenuContent_Lambda([this](UChooserTable* Item)
		{
			return MakeChoosersMenu(Item);
		})
	;

	BreadcrumbTrail->PushCrumb(FText::FromString(RootChooser->GetName()), RootChooser);
	AddHistory();
	
	FCoreUObjectDelegates::OnObjectsReplaced.AddSP(this, &FChooserTableEditor::OnObjectsReplaced);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>( "PropertyEditor" );
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NotifyHook = this;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView( DetailsViewArgs );
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout( "Standalone_ChooserTableEditor_Layout_v1.6" )
	->AddArea
	(
		FTabManager::NewPrimaryArea() ->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewSplitter()
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7)
				->AddTab( TableTabId, ETabState::OpenedTab )
			)
			->Split
			(
			FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.3)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5)
					->AddTab( PropertiesTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5)
					->AddTab( NestedTablesTreeTabId, ETabState::OpenedTab )
				)
			)
		)
	);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, FChooserTableEditor::ChooserEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectsToEdit );

	BindCommands();
	
	// todo: should these be triggered once per session by the module?
	RegisterToolbar();
	RegisterMenus();
	
	RegenerateMenusAndToolbars();

	SelectRootProperties();
	SetChooserTableToEdit(Chooser);
		
	FAnimAssetFindReplaceConfig FindReplaceConfig;
	FindReplaceConfig.InitialProcessorClass = UChooserFindProperties::StaticClass();
	
	FCoreUObjectDelegates::OnObjectTransacted.AddSP(this, &FChooserTableEditor::OnObjectsTransacted);
}

void FChooserTableEditor::FocusWindow(UObject* ObjectToFocusOn)
{
	if (UChooserTable* Chooser = Cast<UChooserTable>(ObjectToFocusOn))
	{
		SetChooserTableToEdit(Chooser);
	}
	// refresh, even if we set the same chooser we were already editing. (Rewind Debugger double click enables debug testing, which requires recreating the header widgets)
	RefreshAll();
	FAssetEditorToolkit::FocusWindow(ObjectToFocusOn);
}

FName FChooserTableEditor::GetToolkitFName() const
{
	return ToolkitFName;
}

FText FChooserTableEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Chooser Table Editor");
}

void FChooserTableEditor::RefreshAll()
{
	if (HeaderRow)
	{
		// Cache Selection state
		ESelectionType CachedSelectionType = CurrentSelectionType;
		int SelectedColumnIndex = -1;
		UChooserTable* SelectedChooser = nullptr;
		TArray<int> CachedSelectedRows;

		if (CachedSelectionType == ESelectionType::Column)
		{
			SelectedColumnIndex = SelectedColumn->Column;
			SelectedChooser = SelectedColumn->Chooser;
		}
		else if (CachedSelectionType == ESelectionType::Rows)
		{
			if (!SelectedRows.IsEmpty())
			{
				SelectedChooser = SelectedRows[0]->Chooser;
			}
			for(const TObjectPtr<UChooserRowDetails>& SelectedRow : SelectedRows)
			{
				CachedSelectedRows.Add(SelectedRow->Row);
			}
		}
		
		UpdateTableColumns();
		UpdateTableRows();

		// reapply cached selection state
		if (CachedSelectionType == ESelectionType::Root)
		{
			SelectRootProperties();
		}
		else if (CachedSelectionType == ESelectionType::Column)
		{
			SelectColumn(SelectedChooser, SelectedColumnIndex);
		}
		else if (CachedSelectionType == ESelectionType::Rows)
		{
			ClearSelectedRows();
			for(int Row : CachedSelectedRows)
			{
				SelectRow(Row, false);
			}
		}
	}
	
	RefreshNestedChoosers();
}

void FChooserTableEditor::RefreshNestedChoosers()
{
	if (NestedChooserTree.IsValid())
	{
		NestedChooserTree->RefreshAll();
	}
}

bool FChooserTableEditor::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	TArray<UObject*> ContainedObjects;
	GetObjectsWithOuter(RootChooser->GetPackage(), ContainedObjects, true);
	
	for(const TPair<UObject*, FTransactionObjectEvent>&  Entry : TransactionObjectContexts)
	{
		if (ContainedObjects.Contains(Entry.Key))
		{
			return true;
		}
	}
	return false;
}

void FChooserTableEditor::PostUndo(bool bSuccess)
{
	RefreshAll();
}

void FChooserTableEditor::PostRedo(bool bSuccess)
{
	RefreshAll();
}


void FChooserTableEditor::NotifyPreChange(FProperty* PropertyAboutToChange)
{
}

void FChooserTableEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	// Called on details panel edits
	
	if (CurrentSelectionType==ESelectionType::Root)
	{
		// Editing the root in the details panel can change ContextData that means all wigets need to be refreshed
		UpdateTableColumns();
		UpdateTableRows();
		SelectRootProperties();
	}
	if (CurrentSelectionType==ESelectionType::Column)
	{
		check(SelectedColumn);
		int SelectedColumnIndex = SelectedColumn->Column;
		UChooserTable* SelectedColumnChooser = SelectedColumn->Chooser;
		// Editing column properties can change the column type, which requires refreshing everything
		UpdateTableColumns();
		UpdateTableRows();
		SelectColumn(SelectedColumnChooser, SelectedColumnIndex);
	}
	// editing row data should not require any refreshing
}

FText FChooserTableEditor::GetToolkitName() const
{
	check( RootChooser );
	return FText::FromString(RootChooser->GetName());
}

FText FChooserTableEditor::GetToolkitToolTipText() const
{
	check( RootChooser );
	return FAssetEditorToolkit::GetToolTipTextForObject(RootChooser);
}

FLinearColor FChooserTableEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.5f, 0.0f, 0.0f, 0.5f );
}

void FChooserTableEditor::SetPropertyVisibilityDelegate(FIsPropertyVisible InVisibilityDelegate)
{
	DetailsView->SetIsPropertyVisibleDelegate(InVisibilityDelegate);
	DetailsView->ForceRefresh();
}

void FChooserTableEditor::SetPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled InPropertyEditingDelegate)
{
	DetailsView->SetIsPropertyEditingEnabledDelegate(InPropertyEditingDelegate);
	DetailsView->ForceRefresh();
}


TSharedRef<SDockTab> FChooserTableEditor::SpawnPropertiesTab( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == PropertiesTabId );

	return SNew(SDockTab)
		.Label( LOCTEXT("GenericDetailsTitle", "Details") )
		.TabColorScale( GetTabColorScale() )
		.OnCanCloseTab_Lambda([]() { return false; })
		[
			DetailsView.ToSharedRef()
		];
}
	
TSharedRef<SDockTab> FChooserTableEditor::SpawnFindReplaceTab( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == FindReplaceTabId );

	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	FAnimAssetFindReplaceConfig Config;
	Config.InitialProcessorClass = UChooserFindProperties::StaticClass();
	return SNew(SDockTab)
		.Label( LOCTEXT("FindReplaceTitle", "Find/Replace") )
		.TabColorScale( GetTabColorScale() )
	[
		PersonaModule.CreateFindReplaceWidget(Config)
	];
}

TSharedRef<ITableRow> FChooserTableEditor::GenerateTableRow(TSharedPtr<FChooserTableRow> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	UChooserTable* Chooser = GetChooser();

	return SNew(SChooserTableRow, OwnerTable)
		.Entry(InItem).Chooser(Chooser).Editor(this);
}

void FChooserTableEditor::SelectRootProperties()
{
	if( DetailsView.IsValid() )
	{
		// point the details view to the main table
		DetailsView->SetObject( GetRootChooser() );
		CurrentSelectionType = ESelectionType::Root;
	}
}

void FChooserTableEditor::RemoveDisabledData()
{
	UChooserTable* Chooser = GetChooser();
	const FScopedTransaction Transaction(LOCTEXT("Remove Disabled Data", "Remove Disabled Data"));

	Chooser->Modify(true);
	Chooser->RemoveDisabledData();
	RefreshAll();
}

int FChooserTableEditor::MoveColumn(int SourceIndex, int TargetIndex)
{
	UChooserTable* Chooser = GetChooser();
	
	TargetIndex = FMath::Clamp(TargetIndex, 0, Chooser->ColumnsStructs.Num());

	if (SourceIndex < 0 || SourceIndex == TargetIndex)
	{
		return TargetIndex;
	}

	const FScopedTransaction Transaction(LOCTEXT("Move Row", "Move Row"));

	Chooser->Modify(true);

	FInstancedStruct ColumnData = Chooser->ColumnsStructs[SourceIndex];
	Chooser->ColumnsStructs.RemoveAt(SourceIndex);
		
	if (SourceIndex < TargetIndex)
	{
		TargetIndex--;
	}
	
	if (TargetIndex == Chooser->ColumnsStructs.Num())
	{
		if (Chooser->ColumnsStructs.Last().GetPtr<FRandomizeColumn>())
		{
			// never drop after a Randomize Column;
			TargetIndex--;
		}
	}

	Chooser->ColumnsStructs.Insert(ColumnData, TargetIndex);

	RefreshAll();

	return TargetIndex;
}

int FChooserTableEditor::MoveRow(int SourceRowIndex, int TargetRowIndex)
{
	UChooserTable* Chooser = GetChooser();
	TargetRowIndex = FMath::Min(TargetRowIndex,Chooser->ResultsStructs.Num());

	const FScopedTransaction Transaction(LOCTEXT("Move Row", "Move Row"));

	Chooser->Modify(true);

	for (FInstancedStruct& ColStruct : Chooser->ColumnsStructs)
	{
		FChooserColumnBase& Column = ColStruct.GetMutable<FChooserColumnBase>();
		Column.MoveRow(SourceRowIndex, TargetRowIndex);
	}

	FInstancedStruct Result = Chooser->ResultsStructs[SourceRowIndex];
	Chooser->ResultsStructs.RemoveAt(SourceRowIndex);
	bool bDisabled = Chooser->DisabledRows[SourceRowIndex];
	Chooser->DisabledRows.RemoveAt(SourceRowIndex);
	if (SourceRowIndex < TargetRowIndex)
	{
		TargetRowIndex--;
	}
	Chooser->ResultsStructs.Insert(Result, TargetRowIndex);
	Chooser->DisabledRows.Insert(bDisabled, TargetRowIndex);
	UpdateTableRows();

	return TargetRowIndex;
}
	
void FChooserTableEditor::SelectRow(int32 RowIndex, bool bClear)
{
	if (TSharedPtr<FChooserTableRow>* Row = TableRows.FindByPredicate([RowIndex](const TSharedPtr<FChooserTableRow>& InRow)
		{
			return InRow->RowIndex == RowIndex;
		}))
	{
		if (!TableView->IsItemSelected(*Row))
		{
			if (bClear)
			{
				TableView->ClearSelection();
			}
			TableView->SetItemSelection(*Row, true, ESelectInfo::OnMouseClick);
		}
	}
}
	
void FChooserTableEditor::ClearSelectedRows() 
{
	SelectedRows.SetNum(0);
	TableView->ClearSelection();
	SelectRootProperties();
}

bool FChooserTableEditor::IsRowSelected(int32 RowIndex)
{
	for(auto& SelectedRow:SelectedRows)
 	{
 		if (SelectedRow->Row == RowIndex)
 		{
 			return true;
 		}
 	}
	return false;
}
	
bool FChooserTableEditor::IsColumnSelected(int32 ColumnIndex)
{
	return  (CurrentSelectionType == ESelectionType::Column && SelectedColumn && SelectedColumn->Column == ColumnIndex);
}

void FChooserTableEditor::UpdateTableColumns()
{
	UChooserTable* Chooser = GetChooser();

	HeaderRow->ClearColumns();

	HeaderRow->AddColumn(SHeaderRow::Column("Handles")
					.DefaultLabel(FText())
					.ManualWidth(30));

	HeaderRow->AddColumn(SHeaderRow::Column("Result")
					.ManualWidth(300)
					.HeaderContent()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Top)
						[
							SNew(STextBlock)
								.Text(LOCTEXT("Result", "Result"))
								.ToolTipText(LOCTEXT("ResultTooltip", "The Result is the asset which will be returned if a row is selected (or other Chooser to evaluate to get the asset to return"))
						]
					]);

	FName ColumnId("ChooserColumn", 1);
	int NumColumns = Chooser->ColumnsStructs.Num();	
	for(int ColumnIndex = 0; ColumnIndex < NumColumns; ColumnIndex++)
	{
		FChooserColumnBase& Column = Chooser->ColumnsStructs[ColumnIndex].GetMutable<FChooserColumnBase>();

		TSharedPtr<SWidget> HeaderWidget = FObjectChooserWidgetFactories::CreateColumnWidget(&Column, Chooser->ColumnsStructs[ColumnIndex].GetScriptStruct(), Chooser->GetRootChooser(), -1);
		if (!HeaderWidget.IsValid())
		{
			HeaderWidget = SNullWidget::NullWidget;
		}
		
		HeaderRow->AddColumn(SHeaderRow::FColumn::FArguments()
			.ColumnId(ColumnId)
			.ManualWidth(200)
			.HeaderComboVisibility(EHeaderComboVisibility::Ghosted)
			.HeaderContent()
			[
				SNew(SChooserColumnHandle)
					.ChooserEditor(this)
					.ColumnIndex(ColumnIndex)
					.NoDropAfter(Chooser->ColumnsStructs[ColumnIndex].GetPtr<FRandomizeColumn>() != nullptr)
				[
					HeaderWidget.ToSharedRef()
				]
			
			]);
	
		ColumnId.SetNumber(ColumnId.GetNumber() + 1);
	}

	HeaderRow->AddColumn( SHeaderRow::FColumn::FArguments()
		.ColumnId("Add")
		.FillWidth(1.0)
		.HeaderContent( )
		[
			SNew(SVerticalBox)
			 + SVerticalBox::Slot().AutoHeight()
			 [
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().MaxWidth(150)
				[
					CreateColumnComboButton.ToSharedRef()
				]
			]
		]
		);

}

void FChooserTableEditor::AddColumn(const UScriptStruct* ColumnType)
{
	FSlateApplication::Get().DismissAllMenus();
	UChooserTable* Chooser = GetChooser();
	const FScopedTransaction Transaction(LOCTEXT("Add Column Transaction", "Add Column"));
	Chooser->Modify(true);

	FInstancedStruct NewColumn;
	NewColumn.InitializeAs(ColumnType);
	const FChooserColumnBase& NewColumnRef = NewColumn.Get<FChooserColumnBase>();
	int InsertIndex = 0;
	if (NewColumnRef.IsRandomizeColumn())
	{
		// add randomization column at the end (do nothing if there already is one)
		InsertIndex = Chooser->ColumnsStructs.Num();
		if (InsertIndex == 0 || !Chooser->ColumnsStructs[InsertIndex - 1].Get<FChooserColumnBase>().IsRandomizeColumn())
		{
			Chooser->ColumnsStructs.Add(NewColumn);
		}
	}
	else if (NewColumnRef.HasOutputs())
	{
		// add output columns at the end (but before any randomization column)
		InsertIndex = Chooser->ColumnsStructs.Num();
		if (InsertIndex > 0 && Chooser->ColumnsStructs[InsertIndex - 1].Get<FChooserColumnBase>().IsRandomizeColumn())
		{
			InsertIndex--;
		}
		Chooser->ColumnsStructs.Insert(NewColumn, InsertIndex);
	}
	else
	{
		// add other columns after the last non-output, non-randomization column
		while(InsertIndex < Chooser->ColumnsStructs.Num())
		{
			const FChooserColumnBase& Column = Chooser->ColumnsStructs[InsertIndex].Get<FChooserColumnBase>();
			if (Column.HasOutputs() || Column.IsRandomizeColumn())
			{
				break;
			}
			InsertIndex++;
		}
		Chooser->ColumnsStructs.Insert(NewColumn, InsertIndex);
	}

	UpdateTableColumns();
	UpdateTableRows();

	SelectColumn(Chooser, InsertIndex);
}

void FChooserTableEditor::RefreshRowSelectionDetails()
{
	for (UObject* SelectedRow : SelectedRows)
	{
		SelectedRow->ClearFlags(RF_Standalone);
	}
	SelectedRows.SetNum(0);
	UChooserTable* Chooser = GetChooser();

	FPropertyBagPropertyDesc ResultPropertyDesc ("Result", EPropertyBagPropertyType::Struct, FInstancedStruct::StaticStruct());
	ResultPropertyDesc.MetaData.Add({"ExcludeBaseStruct",""});
	ResultPropertyDesc.MetaData.Add({"BaseStruct","/Script/Chooser.ObjectChooserBase"});
	
	// Get the list of objects to edit the details of
	TArray<TSharedPtr<FChooserTableRow>> SelectedItems = TableView->GetSelectedItems();
	for(TSharedPtr<FChooserTableRow>& SelectedItem : SelectedItems)
	{
		if (Chooser->ResultsStructs.IsValidIndex(SelectedItem->RowIndex))
		{
			TObjectPtr<UChooserRowDetails> Selection = NewObject<UChooserRowDetails>();
			Selection->Chooser = Chooser;
			Selection->Row = SelectedItem->RowIndex;
			Selection->SetFlags(RF_Standalone | RF_Transactional);

			FInstancedStruct& Result = Chooser->ResultsStructs[SelectedItem->RowIndex];
			Selection->Properties.AddProperties({ResultPropertyDesc});
			Selection->Properties.SetValueStruct("Result", FConstStructView(FInstancedStruct::StaticStruct(), reinterpret_cast<uint8*>(&Result)));

			int ColumnIndex = 0;
			for (FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
			{
				FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
				Column.AddToDetails(Selection->Properties, ColumnIndex, SelectedItem->RowIndex);
				ColumnIndex++;
			}

			if (Chooser->DisabledRows.IsValidIndex(SelectedItem->RowIndex))
			{
				Selection->Properties.AddProperty("Disabled", EPropertyBagPropertyType::Bool);
				Selection->Properties.SetValueBool("Disabled", Chooser->DisabledRows[SelectedItem->RowIndex]);
			}

			SelectedRows.Add(Selection);
		}
		else if (SelectedItem->RowIndex == ColumnWidget_SpecialIndex_Fallback)
		{
			TObjectPtr<UChooserRowDetails> Selection = NewObject<UChooserRowDetails>();
			Selection->Chooser = Chooser;
			Selection->Row = SelectedItem->RowIndex;
			Selection->SetFlags(RF_Standalone | RF_Transactional);

			FInstancedStruct& Result = Chooser->FallbackResult;
			Selection->Properties.AddProperties({ResultPropertyDesc});
			Selection->Properties.SetValueStruct("Result", FConstStructView(FInstancedStruct::StaticStruct(), reinterpret_cast<uint8*>(&Result)));

			int ColumnIndex = 0;
			for (FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
			{
				FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
				if (Column.HasOutputs())
				{
					Column.AddToDetails(Selection->Properties, ColumnIndex, SelectedItem->RowIndex);
				}
				ColumnIndex++;
			}

			SelectedRows.Add(Selection);
		}
	}
	
	TArray<UObject*> DetailsObjects;
	for(auto& Item : SelectedRows)
	{
		DetailsObjects.Add(Item.Get());
	}

	if( DetailsView.IsValid() )
	{
		// Make sure details window is pointing to our object
		DetailsView->SetObjects( DetailsObjects );
	}
}

void FChooserTableEditor::MakeChoosersMenuRecursive(UObject* Outer, FMenuBuilder& MenuBuilder, const FString& Indent = "") 
{
	TArray<UObject*> ChildObjects;
	GetObjectsWithOuter(Outer, ChildObjects, false);

	FString SubIndent = Indent + "    ";
	for (UObject* Object : ChildObjects)
	{
		if (UChooserTable* Chooser = Cast<UChooserTable>(Object))
		{
			if (Chooser == RootChooser || Chooser->GetRootChooser()->NestedChoosers.Contains(Chooser))
			{
				MenuBuilder.AddMenuEntry( FText::FromString(Indent + Chooser->GetName()), LOCTEXT("Edit Chooser ToolTip", "Browse to this Nested Chooser Table"), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Chooser]()
					{
						SetChooserTableToEdit(Chooser);
					})));

				MakeChoosersMenuRecursive(Chooser, MenuBuilder, SubIndent);
			}
		}
	}
}
	
TSharedRef<SWidget> FChooserTableEditor::MakeChoosersMenu(UObject* RootObject)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MakeChoosersMenuRecursive(RootObject, MenuBuilder);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SDockTab> FChooserTableEditor::SpawnNestedTablesTreeTab( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == NestedTablesTreeTabId );

	return SNew(SDockTab)
		.Label( LOCTEXT("NestedChooserTreeTitle", "Nested Choosers") )
		[
			SAssignNew(NestedChooserTree, SNestedChooserTree).ChooserEditor(this)
		];
}

DECLARE_DELEGATE_OneParam(FCreateStructDelegate, UScriptStruct*);

struct FColumnTypeInfoStruct
{
	int SortOrder = 100;
	FString Category;
	UScriptStruct* Type;
	bool operator < (const FColumnTypeInfoStruct& Other) const
	{
		if (Category == Other.Category)
		{
			return Type->GetDisplayNameText().ToString() < Other.Type->GetDisplayNameText().ToString();
		}
		else if (SortOrder == Other.SortOrder)
		{
			return Category < Other.Category;
		}
		else
		{
			return SortOrder < Other.SortOrder;
		}
	}
};

TSharedRef<SWidget>	FChooserTableEditor::MakeCreateColumnMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	static TArray<FColumnTypeInfoStruct> ColumnTypes;

	if (ColumnTypes.IsEmpty())
	{
		UScriptStruct* BaseType = FChooserColumnBase::StaticStruct();
		for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
		{
			if (*StructIt != BaseType && StructIt->IsChildOf(BaseType))
			{
				if (!StructIt->HasMetaData("Hidden"))
				{
					FColumnTypeInfoStruct Info;
					Info.Type = *StructIt;
					Info.Category = StructIt->HasMetaData("Category") ? StructIt->GetMetaData("Category") : "Other";

					if (Info.Category == "Filter")
					{
						Info.SortOrder = 1;
					}
					else if (Info.Category == "Scoring")
                   	{
                   		Info.SortOrder = 2;
                   	}
					else if (Info.Category == "Output")
                   	{
                   		Info.SortOrder = 3;
                   	}
					else if (Info.Category == "Random")
					{
						Info.SortOrder = 4;
					}

					ColumnTypes.Add(Info);
				}
			}
		}
		ColumnTypes.Sort();
	}

	FString Section = "";
	for(FColumnTypeInfoStruct& Type : ColumnTypes)
	{
		if (Section != Type.Category)
		{
			if (Section != "")
			{
				MenuBuilder.EndSection();
			}
			Section = Type.Category;
			MenuBuilder.BeginSection(FName(Section), FText::FromString(Section));
		}
		
		MenuBuilder.AddMenuEntry(Type.Type->GetDisplayNameText(), Type.Type->GetToolTipText(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Type]()
				{
					AddColumn(Type.Type);
				}))
			);
							
	}
	return MenuBuilder.MakeWidget();
}

struct FResultTypeInfoStruct
{
	bool ObjectOnly = false;
	bool ClassOnly = false;
	FString Category;
	UScriptStruct* Type = nullptr;
	
	bool operator < (const FResultTypeInfoStruct& Other) const
	{
		if (Category == Other.Category)
		{
			return Type->GetDisplayNameText().ToString() < Other.Type->GetDisplayNameText().ToString();
		}
		else
		{
			return Category < Other.Category;
		}
	}
};

void MakeCreateResultMenu(FMenuBuilder& MenuBuilder, EObjectChooserResultType ChooserResultType, FCreateStructDelegate CreateStruct)
{
	static TArray<FResultTypeInfoStruct> ResultTypes;

	if (ResultTypes.IsEmpty())
	{
		UScriptStruct* BaseType = FObjectChooserBase::StaticStruct();
		for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
		{
			if (*StructIt != BaseType && StructIt->IsChildOf(BaseType))
			{
				if (!StructIt->HasMetaData("Hidden"))
				{
					FResultTypeInfoStruct Info;
					Info.Type = *StructIt;
					Info.Category = StructIt->HasMetaData("Category") ? StructIt->GetMetaData("Category") : "Other";

					if (StructIt->HasMetaData("ResultType"))
					{
						FString ResultTypeString = StructIt->GetMetaData("ResultType");
						Info.ClassOnly = ResultTypeString == "Class";
						Info.ObjectOnly = ResultTypeString == "Object";
					}
					
					ResultTypes.Add(Info);
				}
			}
		}
		ResultTypes.Sort();
	}

	FString Section = "";
	for(FResultTypeInfoStruct& Type : ResultTypes)
	{
		if (Section != Type.Category)
		{
			if (Section != "")
			{
				MenuBuilder.EndSection();
			}
			Section = Type.Category;
			MenuBuilder.BeginSection(FName(Section), FText::FromString(Section));
		}
		
		MenuBuilder.AddMenuEntry(Type.Type->GetDisplayNameText(), Type.Type->GetToolTipText(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Type, CreateStruct]()
				{
					CreateStruct.Execute(Type.Type);
				}),
				FCanExecuteAction::CreateLambda([Type, ChooserResultType]()
				{
					if (Type.ClassOnly && ChooserResultType == EObjectChooserResultType::ObjectResult)
					{
						return false;
					}
					if (Type.ObjectOnly && ChooserResultType == EObjectChooserResultType::ClassResult)
					{
						return false;
					}
					return true;
				})
				)
			);
							
	}
}

TSharedRef<SWidget>	FChooserTableEditor::MakeCreateRowMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

    UChooserTable* Chooser = GetChooser();
	if (!Chooser->FallbackResult.IsValid())
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("Add Fallback", "Add Fallback Result"),
				LOCTEXT("Add Fallback Tooltip", "Add a Fallback row to the chooser, which will be used in the case where no other rows passed all filter columns"),
				FNewMenuDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
				{
					UChooserTable* Chooser = GetChooser();
					MakeCreateResultMenu(MenuBuilder, Chooser->ResultType, FCreateStructDelegate::CreateLambda([this](UScriptStruct* Type)
					{
						UChooserTable* Chooser = GetChooser();
						const FScopedTransaction Transaction(LOCTEXT("Add Fallback Row Transaction", "Add Fallback Row"));
						Chooser->Modify(true);
						Chooser->FallbackResult.InitializeAs(Type);
						UpdateTableRows();
					}));
				})
			);

	}

	MakeCreateResultMenu(MenuBuilder, Chooser->ResultType, FCreateStructDelegate::CreateLambda([this](UScriptStruct* Type)
	{

		UChooserTable* Chooser = GetChooser();
		const FScopedTransaction Transaction(LOCTEXT("Add Row Transaction", "Add Row"));
		Chooser->Modify(true);
		Chooser->ResultsStructs.SetNum(Chooser->ResultsStructs.Num()+1);
		Chooser->ResultsStructs.Last().InitializeAs(Type);
		UpdateTableRows();
	}));

	return MenuBuilder.MakeWidget();
}

TSharedRef<SDockTab> FChooserTableEditor::SpawnTableTab( const FSpawnTabArgs& Args )
{
	check( Args.GetTabId() == TableTabId );

	UChooserTable* Chooser = GetChooser();

	// + button to create new columns
	
	CreateColumnComboButton = SNew(SPositiveActionButton)
		.Text(LOCTEXT("Add Column", "Add Column"))
		.OnGetMenuContent_Lambda([this]()
		{
			return MakeCreateColumnMenu();
		});

	CreateRowComboButton = SNew(SPositiveActionButton)
		.Text(LOCTEXT("Add Row", "Add Row"))
		.OnGetMenuContent(this, &FChooserTableEditor::MakeCreateRowMenu);
			
	HeaderRow = SNew(SHeaderRow);

	UpdateTableRows();
	UpdateTableColumns();

	TableView = SNew(SListView<TSharedPtr<FChooserTableRow>>)
				.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& Event)
				{
					if (GetToolkitCommands()->ProcessCommandBindings(Event))
					{
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
    			.ListItemsSource(&TableRows)
				.OnContextMenuOpening_Raw(this, &FChooserTableEditor::GenerateRowContextMenu)
				.OnSelectionChanged_Lambda([this](TSharedPtr<FChooserTableRow>,  ESelectInfo::Type SelectInfo)
				{
					// deselect any selected column
					ClearSelectedColumn();

					CurrentSelectionType = ESelectionType::Rows;

					RefreshRowSelectionDetails();
				})
    			.OnGenerateRow_Raw(this, &FChooserTableEditor::GenerateTableRow)
				.HeaderRow(HeaderRow);


	TSharedRef<SComboButton> EditChooserTableButton = SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton");
	
	EditChooserTableButton->SetOnGetMenuContent(
    		FOnGetContent::CreateLambda(
    			[this]()
                		{
							return MakeChoosersMenu(GetRootChooser()->GetPackage());
                		})
    		);

	return SNew(SDockTab)
		.Label( LOCTEXT("ChooserTableTitle", "Chooser Table") )
		.TabColorScale( GetTabColorScale() )
		.OnCanCloseTab_Lambda([]() { return false; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
						.IsEnabled_Raw(this, &FChooserTableEditor::CanNavigateBack)
						.OnClicked_Lambda([this]()
						{
							NavigateBack();
							return FReply::Handled();
						})
						.Content()
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.ArrowLeft"))
						]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "GraphBreadcrumbButton")
						.IsEnabled_Raw(this, &FChooserTableEditor::CanNavigateForward)
						.OnClicked_Lambda([this]()
						{
							NavigateForward();
							return FReply::Handled();
						})
						.Content()
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.ArrowRight") )
						]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					EditChooserTableButton
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					BreadcrumbTrail.ToSharedRef()
				]

				
			]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				SNew(SScrollBox).Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					TableView.ToSharedRef()
				]
			]
		];
}

void FChooserTableEditor::UpdateTableRows()
{
	UChooserTable* Chooser = GetChooser();
	int32 NewNum = Chooser->ResultsStructs.Num();
	Chooser->DisabledRows.SetNum(NewNum);

	// Sync the TableRows array which drives the ui table to match the number of results.
	TableRows.SetNum(0, EAllowShrinking::No);
	for(int i =0; i < NewNum; i++)
	{
		TableRows.Add(MakeShared<FChooserTableRow>(i));
	}

	// Add one at the end, for the Fallback result
	if (Chooser->FallbackResult.IsValid())
	{
		TableRows.Add(MakeShared<FChooserTableRow>(SChooserTableRow::SpecialIndex_Fallback));
	}
	
	// Add one at the end, for the "Add Row" control
	TableRows.Add(MakeShared<FChooserTableRow>(SChooserTableRow::SpecialIndex_AddRow));

	// Make sure each column has the same number of row datas as there are results
	for(FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
	{
		FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
		Column.SetNumRows(NewNum);
	}

	if (TableView.IsValid())
	{
		TableView->RebuildList();
	}
}

void FChooserTableEditor::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	bool bChangedAny = false;

	UObject* ReplacedObject = ReplacementMap.FindRef(RootChooser);

	if (ReplacedObject && ReplacedObject != RootChooser)
	{
		RootChooser = Cast<UChooserTable>(ReplacedObject);
		SetChooserTableToEdit(RootChooser);
		SelectRootProperties();
	}
}

FString FChooserTableEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Chooser Table Asset ").ToString();
}

TSharedRef<FChooserTableEditor> FChooserTableEditor::CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit, FGetDetailsViewObjects GetDetailsViewObjects )
{
	TSharedRef< FChooserTableEditor > NewEditor( new FChooserTableEditor() );

	TArray<UObject*> ObjectsToEdit;
	ObjectsToEdit.Add( ObjectToEdit );
	NewEditor->InitEditor( Mode, InitToolkitHost, ObjectsToEdit, GetDetailsViewObjects );

	return NewEditor;
}

TSharedRef<FChooserTableEditor> FChooserTableEditor::CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects )
{
	TSharedRef< FChooserTableEditor > NewEditor( new FChooserTableEditor() );
	NewEditor->InitEditor( Mode, InitToolkitHost, ObjectsToEdit, GetDetailsViewObjects );
	return NewEditor;
}
	
void FChooserTableEditor::SelectColumn(UChooserTable* ChooserEditor, int Index)
{
	ClearSelectedRows();
	
	UChooserTable* Chooser = GetChooser();
   	if (Index < Chooser->ColumnsStructs.Num())
   	{
   		if (SelectedColumn == nullptr)
   		{
   			SelectedColumn = NewObject<UChooserColumnDetails>();
			SelectedColumn->AddToRoot();
   		}
   
   		SelectedColumn->Chooser = Chooser;
   		SelectedColumn->Column = Index;
   		DetailsView->SetObject(SelectedColumn, true);
   		CurrentSelectionType = ESelectionType::Column;
   	}
   	else
   	{
   		SelectRootProperties();
   	}
}
	
void FChooserTableEditor::ClearSelectedColumn()
{
	UChooserTable* Chooser = GetChooser();
	if (SelectedColumn != nullptr)
	{
		SelectedColumn->Column = -1;
		if (DetailsView->GetSelectedObjects().Contains(SelectedColumn))
		{
			SelectRootProperties();
		}
	}
}
	
void FChooserTableEditor::DeleteColumn(int Index)
{
	const FScopedTransaction Transaction(LOCTEXT("Delete Column Transaction", "Delete Column"));
	ClearSelectedColumn();
	SelectRootProperties();
	UChooserTable* Chooser = GetChooser();

	if (Index < Chooser->ColumnsStructs.Num())
	{
		Chooser->Modify(true);
		Chooser->ColumnsStructs.RemoveAt(Index);
		UpdateTableColumns();
	}
}
	
int32 FChooserTableEditor::DeleteSelectedRows(int32 RowIndexToRemember)
{
	const FScopedTransaction Transaction(LOCTEXT("Delete Row Transaction", "Delete Row"));
	return DeleteSelectedRowsInternal(RowIndexToRemember);
}
	
int32 FChooserTableEditor::DeleteSelectedRowsInternal(int32 RowIndexToRemember)
{
	UChooserTable* Chooser = GetChooser();
	Chooser->Modify(true);
	// delete selected rows.
	TArray<uint32> RowsToDelete;
	for(auto& SelectedRow:SelectedRows)
	{
		if (SelectedRow->Row == ColumnWidget_SpecialIndex_Fallback)
		{
			Chooser->FallbackResult.Reset();
		}
		else
		{
			RowsToDelete.Add(SelectedRow->Row);
		}
	}

	SelectedRows.SetNum(0);
	SelectRootProperties();

	// sort indices in reverse
	RowsToDelete.Sort([](int32 A, int32 B){ return A>B; });
	for(int32 RowIndex : RowsToDelete)
	{
		if (RowIndex <= RowIndexToRemember)
		{
			RowIndexToRemember--;
		}
		Chooser->ResultsStructs.RemoveAt(RowIndex);
		Chooser->DisabledRows.RemoveAt(RowIndex);
	}

	for(FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
	{
		FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
		Column.DeleteRows(RowsToDelete);
	}
	UpdateTableRows();

	return RowIndexToRemember;
}

void FChooserTableEditor::MoveRows(int TargetIndex)
{
	const FScopedTransaction Transaction(LOCTEXT("Move Row(s)", "Move Row(s)"));
	UChooserTable* RowCopy = CopySelectionInternal();
	TargetIndex = DeleteSelectedRowsInternal(TargetIndex);
	PasteInternal(RowCopy, TargetIndex);
}

void FChooserTableEditor::AutoPopulateColumn(FChooserColumnBase& Column)
{
	if (UChooserTable* Chooser = GetChooser())
	{
		const int RowCount = Chooser->ResultsStructs.Num();
		if (Column.AutoPopulates())
		{
			for (int i = 0; i < RowCount; ++i)
			{
				if (Chooser->ResultsStructs[i].IsValid())
				{
					if (UObject* ReferencedObject = Chooser->ResultsStructs[i].Get<FObjectChooserBase>().GetReferencedObject()) 
					{
						Column.AutoPopulate(i, ReferencedObject);
					}
				}
			}
		}
	}
}

void FChooserTableEditor::AutoPopulateRow(int Index)
{
	if (UChooserTable* Chooser = GetChooser())
	{
		if (Chooser->ResultsStructs.IsValidIndex(Index) && Chooser->ResultsStructs[Index].IsValid())
		{
			if (UObject* ReferencedObject = Chooser->ResultsStructs[Index].Get<FObjectChooserBase>().GetReferencedObject()) 
			{
				for (FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
				{
					FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
					Column.AutoPopulate(Index, ReferencedObject);
				}
			}
		}
	}
}
	
bool FChooserTableEditor::CanAutoPopulateSelection()
{
	if (UChooserTable* Chooser = GetChooser())
	{
		if (CurrentSelectionType == ESelectionType::Column && SelectedColumn)
		{
			// when a column is selected, return true if that column supports auto populate
			if (Chooser->ColumnsStructs.IsValidIndex(SelectedColumn->Column))
			{
				return Chooser->ColumnsStructs[SelectedColumn->Column].Get<FChooserColumnBase>().AutoPopulates();
			}	
		}
		else
		{
			if (SelectedRows.IsEmpty())
			{
				return false;
			}
			
			// when rows are selected, return true if any column supports auto populate
			for (FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
			{
				const FChooserColumnBase& Column = ColumnData.Get<FChooserColumnBase>();
				if (Column.AutoPopulates())
				{
					return true;
				}
			}
		}
	}
	
	return false;
}
	
void FChooserTableEditor::AutoPopulateSelection()
{
	if (UChooserTable* Chooser = GetChooser())
	{
		const FScopedTransaction Transaction(LOCTEXT("Auto Populate Selection", "Auto Populate Selection"));
		Chooser->Modify();
		if (HasColumnSelected())
		{
			if (Chooser->ColumnsStructs.IsValidIndex(SelectedColumn->Column))
			{
				AutoPopulateColumn(Chooser->ColumnsStructs[SelectedColumn->Column].GetMutable<FChooserColumnBase>());
			}
		}
		else if (HasRowsSelected())
		{
			for(UChooserRowDetails* RowDetails : SelectedRows)
			{
				AutoPopulateRow(RowDetails->Row);
			}
		}
	}
}

void FChooserTableEditor::AutoPopulateAll()
{
	if (UChooserTable* Chooser = GetChooser())
	{
		const FScopedTransaction Transaction(LOCTEXT("Auto Populate Chooser", "Auto Populate All"));
		Chooser->Modify();
		for (FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
		{
			FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
			AutoPopulateColumn(Column);
		}
	}
}

bool FChooserTableEditor::HasSelection() const
{
	return HasRowsSelected() || HasColumnSelected();
}

bool FChooserTableEditor::HasRowsSelected() const
{
	return CurrentSelectionType == ESelectionType::Rows && !SelectedRows.IsEmpty();
}
	
bool FChooserTableEditor::HasColumnSelected() const
{
	const UChooserTable* Chooser = GetChooser();
	return CurrentSelectionType == ESelectionType::Column && SelectedColumn && Chooser->ColumnsStructs.IsValidIndex(SelectedColumn->Column);
}

bool FChooserTableEditor::IsSelectionDisabled() const
{
	if (const UChooserTable* Chooser = GetChooser())
	{
		if (HasColumnSelected())
		{
			if ( Chooser->ColumnsStructs.IsValidIndex(SelectedColumn->Column))
			{
				const FChooserColumnBase& Column = Chooser->ColumnsStructs[SelectedColumn->Column].Get<FChooserColumnBase>();
				return Column.bDisabled;
			}
		}
		else if (HasRowsSelected())
		{
			bool bSomethingEnabled = false;
			for(auto& Row : SelectedRows)
			{
				if (!Chooser->IsRowDisabled(Row->Row))
				{
					bSomethingEnabled = true;
					break;
				}
			}
			return !bSomethingEnabled;
		}
	}
	return false;
}

void FChooserTableEditor::ToggleDisableSelection()
{
	bool bDisabled = IsSelectionDisabled();
	if (UChooserTable* Chooser = GetChooser())
	{
		if (HasColumnSelected())
		{
			if (Chooser->ColumnsStructs.IsValidIndex(SelectedColumn->Column))
			{
				FChooserColumnBase& Column = Chooser->ColumnsStructs[SelectedColumn->Column].GetMutable<FChooserColumnBase>();
				Column.bDisabled = !Column.bDisabled;
			}
		}
		else if (HasRowsSelected())
		{
			for (auto& Row : SelectedRows)
			{
				if (Chooser->DisabledRows.IsValidIndex(Row->Row))
				{
					Chooser->DisabledRows[Row->Row] = !bDisabled;
				}
			}
			RefreshRowSelectionDetails();
		}
	}
}

void FChooserTableEditor::DeleteSelection()
{
	if (HasColumnSelected())
	{
		DeleteColumn(SelectedColumn->Column);
	}
	else if (HasRowsSelected())
	{
		DeleteSelectedRows();
	}
}

void FChooserTableEditor::DuplicateSelection()
{
	if (HasRowsSelected())
	{
		const FScopedTransaction Transaction(LOCTEXT("Duplicate Row(s)", "Duplicate Row(s)"));
		UChooserTable* RowCopy = CopySelectionInternal();
		int MaxSelectedRow = -1;
		for(UChooserRowDetails* SelectedRow : SelectedRows)
		{
			MaxSelectedRow = FMath::Max(SelectedRow->Row, MaxSelectedRow);
		}
		PasteInternal(RowCopy, MaxSelectedRow + 1);
	}
	else if(HasColumnSelected())
	{
		const FScopedTransaction Transaction(LOCTEXT("Duplicate Column", "Duplicate Column"));
		UChooserTable* Chooser = GetChooser();
		Chooser->Modify();
		FInstancedStruct Column = Chooser->ColumnsStructs[SelectedColumn->Column];
		Chooser->ColumnsStructs.Insert(Column,SelectedColumn->Column);
		RefreshAll();
	}
}

bool FChooserTableEditor::HasFallbackSelected()
{
	for(UChooserRowDetails* SelectedRow : SelectedRows)
	{
		if (SelectedRow->Row == ColumnWidget_SpecialIndex_Fallback)
		{
			return true;
		}
	}

	return false;
}

bool FChooserTableEditor::CanMoveRowsUp()
{
	if (HasRowsSelected())
	{
		UChooserTable* Chooser = GetChooser();

		int MinSelectedRow = Chooser->ResultsStructs.Num();
		for(UChooserRowDetails* SelectedRow : SelectedRows)
		{
			if (SelectedRow->Row != ColumnWidget_SpecialIndex_Fallback)
			{
				MinSelectedRow = FMath::Min(SelectedRow->Row, MinSelectedRow);
			}
		}

		return MinSelectedRow > 0;
	}
	return false;
}

void FChooserTableEditor::MoveRowsUp()
{
	if (HasRowsSelected())
	{
		UChooserTable* Chooser = GetChooser();
		int MinSelectedRow = Chooser->ResultsStructs.Num();
		for(UChooserRowDetails* SelectedRow : SelectedRows)
		{
			if (SelectedRow->Row != ColumnWidget_SpecialIndex_Fallback)
			{
				MinSelectedRow = FMath::Min(SelectedRow->Row, MinSelectedRow);
			}
		}
		MoveRows(MinSelectedRow - 1);
	}
}

bool FChooserTableEditor::CanMoveRowsDown()
{
	if (HasRowsSelected())
	{
		UChooserTable* Chooser = GetChooser();
		
		int MaxSelectedRow = -1;
		for(UChooserRowDetails* SelectedRow : SelectedRows)
		{
			MaxSelectedRow = FMath::Max(SelectedRow->Row, MaxSelectedRow);
		}

		return MaxSelectedRow < Chooser->ResultsStructs.Num() - 1;
	}
	return false;
}

void FChooserTableEditor::MoveRowsDown()
{
	if (HasRowsSelected())
   	{
   		UChooserTable* Chooser = GetChooser();
   		int MaxSelectedRow = -1;
   		for(UChooserRowDetails* SelectedRow : SelectedRows)
   		{
   			MaxSelectedRow = FMath::Max(SelectedRow->Row, MaxSelectedRow);
   		}
   		MoveRows(MaxSelectedRow + 2);
   	}
}

bool FChooserTableEditor::CanMoveColumnLeft()
{
	if (HasColumnSelected())
	{
		UChooserTable* Chooser = GetChooser();
		
		if (Chooser->ColumnsStructs[SelectedColumn->Column].GetPtr<FRandomizeColumn>())
		{
			return false;
		}
		
		return SelectedColumn->Column > 0;
	}
	return false;
}

void FChooserTableEditor::MoveColumnLeft()
{
	if (CanMoveColumnLeft())
	{
		SelectColumn(GetChooser(), MoveColumn(SelectedColumn->Column, SelectedColumn->Column - 1));
	}
}

bool FChooserTableEditor::CanMoveColumnRight()
{
	if (HasColumnSelected())
   	{
		UChooserTable* Chooser = GetChooser();
		
		if (Chooser->ColumnsStructs[SelectedColumn->Column].GetPtr<FRandomizeColumn>())
		{
			return false;
		}
		int NumColumns = Chooser->ColumnsStructs.Num();
		if (NumColumns > 0 && Chooser->ColumnsStructs.Last().GetPtr<FRandomizeColumn>())
		{
			NumColumns--;
		}
   		return (SelectedColumn->Column < NumColumns-1);
   	}
   	return false;
}

void FChooserTableEditor::MoveColumnRight()
{
	if (CanMoveColumnRight())
	{
		SelectColumn(GetChooser(), MoveColumn(SelectedColumn->Column, SelectedColumn->Column + 2));
	}
}

UChooserTable* DuplicateNestedChooser(UChooserTable* Chooser, UChooserTable* NewOuter)
{
	UChooserTable* RootTable = NewOuter->GetRootChooser();
	if (TObjectPtr<UChooserTable>* FoundTable = RootTable->NestedChoosers.FindByPredicate([Chooser](UChooserTable* Table)
		{
			return Table->GetName() == Chooser->GetName();
		}))
	{
		// we already duplicated this table
		return *FoundTable;
	}
	
	UChooserTable* NewTable = NewObject<UChooserTable>(NewOuter, Chooser->GetFName());
	NewTable->ColumnsStructs = Chooser->ColumnsStructs;
	NewTable->ResultsStructs = Chooser->ResultsStructs;
	NewTable->RootChooser = RootTable;
	RootTable->AddNestedChooser(NewTable);

	for (FInstancedStruct& ResultData : NewTable->ResultsStructs)
	{
		if (FNestedChooser* NestedChooser = ResultData.GetMutablePtr<FNestedChooser>())
		{
			NestedChooser->Chooser = DuplicateNestedChooser(NestedChooser->Chooser, NewOuter);
		}
	}

	return NewTable;
}
	
UChooserTable* FChooserTableEditor::CopySelectionInternal()
{
	UChooserTable* CopyData = NewObject<UChooserTable>(GetTransientPackage());

	UChooserTable* Chooser = GetChooser();

	// copy context data from root table
	CopyData->OutputObjectType = RootChooser->OutputObjectType;
	CopyData->ResultType = RootChooser->ResultType;
	CopyData->ContextData = RootChooser->ContextData;

	if (CurrentSelectionType == ESelectionType::Column)
	{
		// add selected column including all the cell data
		CopyData->ColumnsStructs.Add(Chooser->ColumnsStructs[SelectedColumn->Column]);
	}
	else if (CurrentSelectionType == ESelectionType::Rows)
	{
		TArray<UChooserRowDetails*> SelectedRowsCopy = SelectedRows;
		Algo::Sort(SelectedRowsCopy, [](UChooserRowDetails *A, UChooserRowDetails *B) { return A->Row<B->Row;});
		
		// copy all columns
		CopyData->ColumnsStructs = Chooser->ColumnsStructs;
		
		// clear all column's cell data
		for (FInstancedStruct& ColumnData : CopyData->ColumnsStructs)
		{
			FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
			Column.SetNumRows(0);
			Column.SetNumRows(SelectedRowsCopy.Num());
		}
		
		if (SelectedRowsCopy.Num() > 0 && SelectedRowsCopy[0]->Row == ColumnWidget_SpecialIndex_Fallback)
		{
			SelectedRowsCopy.RemoveAt(0);
			
			CopyData->FallbackResult = Chooser->FallbackResult;
			if (FNestedChooser* CopiedNestedChooser = CopyData->FallbackResult.GetMutablePtr<FNestedChooser>())
			{
				// if the fallback result was a nested chooser, duplicate it
				CopiedNestedChooser->Chooser = DuplicateNestedChooser(CopiedNestedChooser->Chooser, CopyData);
			}
		}
		
		CopyData->ResultsStructs.SetNum(SelectedRowsCopy.Num());
		CopyData->DisabledRows.SetNum(SelectedRowsCopy.Num());

		// add the selected results and column data
		
		for (int RowIndex = 0; RowIndex < SelectedRowsCopy.Num(); RowIndex++)
		{
			CopyData->ResultsStructs[RowIndex] = Chooser->ResultsStructs[SelectedRowsCopy[RowIndex]->Row];
			if (FNestedChooser* CopiedNestedChooser = CopyData->ResultsStructs[RowIndex].GetMutablePtr<FNestedChooser>())
			{
				if (CopiedNestedChooser->Chooser)
				{
					// if the result for this row was a nested chooser (with a valid chooser assigned), duplicate it
					CopiedNestedChooser->Chooser = DuplicateNestedChooser(CopiedNestedChooser->Chooser, CopyData);
				}
			}
			
			CopyData->DisabledRows[RowIndex] = Chooser->DisabledRows[SelectedRowsCopy[RowIndex]->Row];
			
			for (int ColumnIndex = 0; ColumnIndex < CopyData->ColumnsStructs.Num(); ColumnIndex++)
			{
				FChooserColumnBase& SourceColumn = Chooser->ColumnsStructs[ColumnIndex].GetMutable<FChooserColumnBase>();
				FChooserColumnBase& TargetColumn = CopyData->ColumnsStructs[ColumnIndex].GetMutable<FChooserColumnBase>();
				TargetColumn.CopyRow(SourceColumn, SelectedRowsCopy[RowIndex]->Row, RowIndex);
			}
		}
	}
	
	return CopyData;
}

void FChooserTableEditor::CopySelection()
{
	UChooserTable* CopyData = CopySelectionInternal();

	// Clear the mark state for saving.
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));
	
	// Export the clipboard to text.
	FStringOutputDevice Archive;
	const FExportObjectInnerContext Context;
	UExporter::ExportToOutputDevice(&Context,CopyData, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, CopyData->GetOuter());
	FPlatformApplicationMisc::ClipboardCopy(*Archive);
}
	
void FChooserTableEditor::CutSelection()
{
	CopySelection();
	DeleteSelection();
}

class FChooserClipboardFactory : public FCustomizableTextObjectFactory
{
public:
	
	FChooserClipboardFactory()
		: FCustomizableTextObjectFactory(GWarn)
		, ClipboardContent(nullptr) 
	{
	}

	UChooserTable* ClipboardContent;
	
protected:

	virtual bool CanCreateClass(UClass* InObjectClass, bool& bOmitSubObjs) const override
	{
		if (InObjectClass->IsChildOf(UChooserTable::StaticClass()))
		{
			return true;
		}
		return false;
	}
	
	virtual void ProcessConstructedObject(UObject* CreatedObject) override
	{
		if (CreatedObject->IsA<UChooserTable>())
		{
			ClipboardContent = CastChecked<UChooserTable>(CreatedObject);
		}
	}
};

static FString GetColumnName(FChooserColumnBase& Column)
{
	if (FChooserParameterBase* InputValue = Column.GetInputValue())
	{
		return InputValue->GetDebugName();
	}
	return FString();
}
	
void FChooserTableEditor::PasteInternal(UChooserTable* PastedContent, int PasteRowIndex)
{
	UChooserTable* Chooser = GetChooser();
	Chooser->Modify();
	
	if (PastedContent->ResultsStructs.IsEmpty() && !PastedContent->FallbackResult.IsValid())
	{
		// pasting a column
		int InsertColumnIndex = Chooser->ColumnsStructs.Num();
		if (CurrentSelectionType == ESelectionType::Column && SelectedColumn)
		{
			InsertColumnIndex = FMath::Min(InsertColumnIndex, SelectedColumn->Column + 1);
		}
		
		if (Chooser->ColumnsStructs.Num() > 0 && Chooser->ColumnsStructs.Num() == InsertColumnIndex)
		{
			// if were inserting at the end, there is a randomize column, insert new columns before it
			if (Chooser->ColumnsStructs.Last().GetPtr<FRandomizeColumn>())
			{
				InsertColumnIndex--;
			}
		}
		Chooser->ColumnsStructs.Insert(PastedContent->ColumnsStructs, InsertColumnIndex);
		SelectColumn(Chooser, InsertColumnIndex);
	}
	else
	{
		// pasting rows
		int RowsToPaste = PastedContent->ResultsStructs.Num();

		// figure out where to start inserting
		int InsertIndex = Chooser->ResultsStructs.Num();

		if (PasteRowIndex >=0)
		{
			InsertIndex = PasteRowIndex;
		}
		else
		{
			if (SelectedRows.Num() > 0)
			{
				InsertIndex = SelectedRows[0]->Row;
				for(int SelectedRowIndex = 1; SelectedRowIndex < SelectedRows.Num(); SelectedRowIndex ++)
				{
					InsertIndex = FMath::Max(InsertIndex, SelectedRows[SelectedRowIndex]->Row);
				}
				if (InsertIndex == ColumnWidget_SpecialIndex_Fallback)
				{
					// if the only row selected was the fallback, reset insert index to the last row
					InsertIndex = Chooser->ResultsStructs.Num();
				}
				else
				{
					InsertIndex++;
				}
			}
		}

		if (PastedContent->ResultsStructs.Num() > 0)
		{
			Chooser->ResultsStructs.Insert(PastedContent->ResultsStructs, InsertIndex);
			Chooser->DisabledRows.Insert(PastedContent->DisabledRows, InsertIndex);

			// Make sure each column has the same number of row datas as there are results
			for(FInstancedStruct& ColumnData : Chooser->ColumnsStructs)
			{
				FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
				Column.InsertRows(InsertIndex, RowsToPaste);
			}
		}
		if (PastedContent->FallbackResult.IsValid())
		{
			// paste fallback result if copy data has one
			Chooser->FallbackResult = PastedContent->FallbackResult;
			if (FNestedChooser* NestedChooser = Chooser->FallbackResult.GetMutablePtr<FNestedChooser>())
			{
				// duplicate the nested chooser if the fallback result refers to a nested chooser
				NestedChooser->Chooser = DuplicateNestedChooser(NestedChooser->Chooser, Chooser);
			}
		}
		
		if (!PastedContent->NestedChoosers.IsEmpty())
		{
			// if there were nested choosers in the copy buffer we have to remap or paste them here
			for (int ResultIndex = InsertIndex; ResultIndex < PastedContent->ResultsStructs.Num() + InsertIndex; ResultIndex++)
			{
				if (FNestedChooser* NestedChooser = Chooser->ResultsStructs[ResultIndex].GetMutablePtr<FNestedChooser>())
				{
					NestedChooser->Chooser = DuplicateNestedChooser(NestedChooser->Chooser, Chooser);
				}
			}
		}

		// try to also paste column data from columns in the paste buffer which match the columns in the current chooser
		// -- matching by column type and input value name

		// keep track of target columns that have already been matched, to avoid matching multiple source columns with the same target column
		TArray<bool> MatchedTargetColumns;
		MatchedTargetColumns.SetNum(Chooser->ColumnsStructs.Num());

		// keep track of which source columns were matched, so we can add new columns for the unmatched ones after
		TArray<bool> MatchedSourceColumns;
		MatchedSourceColumns.SetNum(PastedContent->ColumnsStructs.Num());
		
		for(int SourceColumnIndex = 0; SourceColumnIndex < PastedContent->ColumnsStructs.Num(); SourceColumnIndex++)
		{
			FInstancedStruct& PastedColumnData = PastedContent->ColumnsStructs[SourceColumnIndex];
			FChooserColumnBase& PastedColumn = PastedColumnData.GetMutable<FChooserColumnBase>();
			FString PastedColumnName = GetColumnName(PastedColumn);
			if (FChooserParameterBase* PastedColumnInput = PastedColumn.GetInputValue())
			{
				for(int TargetColumnIndex = 0; TargetColumnIndex < Chooser->ColumnsStructs.Num(); TargetColumnIndex++)
				{
					if (!MatchedTargetColumns[TargetColumnIndex])
					{
						FInstancedStruct& ColumnData = Chooser->ColumnsStructs[TargetColumnIndex];
						if (ColumnData.GetScriptStruct() == PastedColumnData.GetScriptStruct())
						{
							FChooserColumnBase& Column = ColumnData.GetMutable<FChooserColumnBase>();
							FString ColumnName = GetColumnName(Column);

							if (ColumnName == PastedColumnName)
							{
								MatchedTargetColumns[TargetColumnIndex] = true;
								MatchedSourceColumns[SourceColumnIndex] = true;

								// found a match, copy the data over
								for (int i = 0; i < RowsToPaste; i++)
								{
									Column.CopyRow(PastedColumn, i, InsertIndex + i);
								}

								if (PastedContent->FallbackResult.IsValid())
								{
									// if the fallback row was copied, paste the fallback data for columns
									Column.CopyFallback(PastedColumn);
								}
								break;
							}
						}
					}
				}
			}
		}
		
		// add new columns for any source columns that were unmatched
		
		int InsertColumnIndex = Chooser->ColumnsStructs.Num();
		if (Chooser->ColumnsStructs.Num() > 0)
		{
			// if there is a randomize column, insert new columns before it
			if (Chooser->ColumnsStructs.Last().GetPtr<FRandomizeColumn>())
			{
				InsertColumnIndex--;
			}
		}

		for(int SourceColumnIndex = 0; SourceColumnIndex < PastedContent->ColumnsStructs.Num(); SourceColumnIndex++)
		{
			if (!MatchedSourceColumns[SourceColumnIndex])
			{
				FInstancedStruct& PastedColumnData = PastedContent->ColumnsStructs[SourceColumnIndex];
				FChooserColumnBase& PastedColumn = PastedColumnData.GetMutable<FChooserColumnBase>();
				// if we couldn't find a match, paste a new column
				Chooser->ColumnsStructs.Insert(PastedColumnData, InsertColumnIndex);
				FChooserColumnBase& Column = Chooser->ColumnsStructs[InsertColumnIndex].GetMutable<FChooserColumnBase>();
				InsertColumnIndex++;
				Column.SetNumRows(0);
				Column.SetNumRows(Chooser->ResultsStructs.Num());
				for (int i = 0; i < RowsToPaste; i++)
				{
					Column.CopyRow(PastedColumn, i, InsertIndex + i);
				}
			}
		}

		RefreshAll();
		// select the inserted rows
		ClearSelectedRows();
		for (int i = 0; i < RowsToPaste; i++)
		{
			SelectRow(InsertIndex + i, false);
		}
		if(PastedContent->FallbackResult.IsValid())
		{
			SelectRow(ColumnWidget_SpecialIndex_Fallback, false);
		}
	}
	
	RefreshAll();
}
	
void FChooserTableEditor::Paste()
{
	// Get the text from the clipboard.
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	FChooserClipboardFactory Factory;

	UChooserTable* Chooser = GetChooser();

	if (Factory.CanCreateObjectsFromText(ClipboardText))
	{
		Factory.ProcessBuffer((UObject*)GetTransientPackage(), RF_Transactional, ClipboardText);
		if (UChooserTable* PastedContent = Factory.ClipboardContent)
		{
			FScopedTransaction Transaction(LOCTEXT("Paste Chooser Data", "Paste Chooser Data"));
			PasteInternal(PastedContent);
		}
	}
}

	
bool FChooserTableEditor::CanPaste() const
{
	// Get the text from the clipboard.
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	FChooserClipboardFactory Factory;
	return Factory.CanCreateObjectsFromText(ClipboardText); 
}

void FChooserTableEditor::RegisterWidgets()
{
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FAssetChooser::StaticStruct(), CreateAssetWidget);
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FSoftAssetChooser::StaticStruct(), CreateSoftAssetWidget);
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FClassChooser::StaticStruct(), CreateClassWidget);
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FEvaluateChooser::StaticStruct(), CreateEvaluateChooserWidget);
	FObjectChooserWidgetFactories::RegisterWidgetCreator(FNestedChooser::StaticStruct(), CreateNestedChooserWidget);

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	
	PropertyModule.RegisterCustomClassLayout("ChooserTable", FOnGetDetailCustomizationInstance::CreateStatic(&FChooserDetails::MakeInstance));	
	PropertyModule.RegisterCustomClassLayout("ChooserRowDetails", FOnGetDetailCustomizationInstance::CreateStatic(&FChooserRowDetails::MakeInstance));	
	PropertyModule.RegisterCustomClassLayout("ChooserColumnDetails", FOnGetDetailCustomizationInstance::CreateStatic(&FChooserColumnDetails::MakeInstance));	
}
}

#undef LOCTEXT_NAMESPACE

