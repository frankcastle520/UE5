// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chooser.h"
#include "EditorUndoClient.h"
#include "PropertyEditorDelegates.h"
#include "SNestedChooserTree.h"
#include "Containers/RingBuffer.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableViewBase.h"
#include "ChooserTableEditor.generated.h"

class SComboButton;
class SPositiveActionButton;
class SEditableText;
class IDetailsView;
class UChooserRowDetails;

// Class used for chooser editor details customization
UCLASS()
class UChooserColumnDetails : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Instanced, Category="Hidden")
	TObjectPtr<UChooserTable> Chooser;
	int Column = -1;
};


namespace UE::ChooserEditor
{
	class SNestedChooserTree;
	struct FChooserTableRow;
	
	class FChooserTableEditor : public FAssetEditorToolkit, public FSelfRegisteringEditorUndoClient, public FNotifyHook
	{
	public:
		/** Delegate that, given an array of assets, returns an array of objects to use in the details view of an FSimpleAssetEditor */
		DECLARE_DELEGATE_RetVal_OneParam(TArray<UObject*>, FGetDetailsViewObjects, const TArray<UObject*>&);

		virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
		virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

		/**
		* Edits the specified asset object
		*
		* @param	Mode					Asset editing mode for this editor (standalone or world-centric)
		* @param	InitToolkitHost			When Mode is WorldCentric, this is the level editor instance to spawn this editor within
		* @param	ObjectsToEdit			The object to edit
		* @param	GetDetailsViewObjects	If bound, a delegate to get the array of objects to use in the details view; uses ObjectsToEdit if not bound
		*/
		void InitEditor( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects );

		virtual void FocusWindow(UObject* ObjectToFocusOn) override;

		/** Destructor */
		virtual ~FChooserTableEditor();

		virtual FName GetEditorName() const override;

		/** IToolkit interface */
		virtual FName GetToolkitFName() const override;
		virtual FText GetBaseToolkitName() const override;
		virtual FText GetToolkitName() const override;
		virtual FText GetToolkitToolTipText() const override;
		virtual FString GetWorldCentricTabPrefix() const override;
		virtual FLinearColor GetWorldCentricTabColorScale() const override;
		virtual bool IsPrimaryEditor() const override { return true; }
		virtual bool IsSimpleAssetEditor() const override { return false; }
		virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;

		/** FEditorUndoClient Interface */
		virtual bool MatchesContext( const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts ) const override;
		virtual void PostUndo(bool bSuccess) override;
		virtual void PostRedo(bool bSuccess) override;
		
		/** Begin FNotifyHook Interface */
		virtual void NotifyPreChange( FProperty* PropertyAboutToChange ) override;
		virtual void NotifyPostChange( const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

		const UChooserTable* GetRootChooser() const { return RootChooser; }
		UChooserTable* GetRootChooser() { return RootChooser; }
		UChooserTable* GetChooser() { return BreadcrumbTrail->PeekCrumb(); }
		const UChooserTable* GetChooser() const { return BreadcrumbTrail->PeekCrumb(); }

		void PushChooserTableToEdit(UChooserTable* Chooser);
		void PopChooserTableToEdit();
		void RefreshAll();
		void RefreshNestedChoosers();
	
		/** Used to show or hide certain properties */
		void SetPropertyVisibilityDelegate(FIsPropertyVisible InVisibilityDelegate);
		/** Can be used to disable the details view making it read-only */
		void SetPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled InPropertyEditingDelegate);

		bool HasSelection() const;
		bool HasRowsSelected() const;
		bool HasColumnSelected() const;
		bool IsSelectionDisabled() const;
		void ToggleDisableSelection();
		void DeleteSelection();
		void DuplicateSelection();
		bool HasFallbackSelected();

		bool CanMoveRowsUp();
		void MoveRowsUp();
		bool CanMoveRowsDown();
		void MoveRowsDown();
		bool CanMoveColumnLeft();
		void MoveColumnLeft();
		bool CanMoveColumnRight();
		void MoveColumnRight();
		
		void CopySelection();
        void CutSelection();
        		

		void AutoPopulateColumn(FChooserColumnBase& Column);
		void AutoPopulateRow(int Index);
		bool CanAutoPopulateSelection();
		void AutoPopulateSelection();
		void AutoPopulateAll();
		
		void UpdateTableRows();
		void SelectColumn(UChooserTable* Chooser, int Index);
		void ClearSelectedColumn();
		void DeleteColumn(int Index);
		void AddColumn(const UScriptStruct* ColumnType);
		void RefreshRowSelectionDetails();
		TSharedRef<SWidget> MakeChoosersMenu(UObject* RootObject);
		void MakeChoosersMenuRecursive(UObject* Outer, FMenuBuilder& MenuBuilder, const FString& Indent);
		int32 DeleteSelectedRows(int32 RowIndexToRemember = 0);
		int32 DeleteSelectedRowsInternal(int32 RowIndexToRemember = 0);
		void MoveRows(int TargetIndex);
		int MoveRow(int SourceRowIndex, int TargetIndex);
		int MoveColumn(int SourceIndex, int TargetIndex);
		void SelectRow(int32 RowIndex, bool bClear = true);
		void ClearSelectedRows(); 
		bool IsRowSelected(int32 RowIndex);
		bool IsColumnSelected(int32 ColumnIndex);
		
		void SetChooserTableToEdit(UChooserTable* Chooser, bool bApplyToHistory = true);

		enum class ESelectionType
		{
			Root, Rows, Column
		};
		
		ESelectionType GetCurrentSelectionType() const { return CurrentSelectionType; }

		UChooserTable* CopySelectionInternal();
		void PasteInternal(UChooserTable* PasteObject, int PasteRowIndex=-1);

		bool TableHasFocus() const { return TableView->HasKeyboardFocus(); }
	private:
		
		void SelectRootProperties();
		void RemoveDisabledData();
		void RegisterToolbar();
		void RegisterMenus();


		void Paste();
		bool CanPaste() const;
		void BindCommands();
		void OnObjectsTransacted(UObject* Object, const FTransactionObjectEvent& Event);
		void MakeDebugTargetMenu(UToolMenu* InToolMenu);
		TSharedPtr<SWidget> GenerateRowContextMenu();
	
		/** Create the properties tab and its content */
		TSharedRef<SDockTab> SpawnPropertiesTab( const FSpawnTabArgs& Args );
		/** Create the table tab and its content */
		TSharedRef<SDockTab> SpawnTableTab( const FSpawnTabArgs& Args );
		/** Create the find/replace tab and its content */
		TSharedRef<SDockTab> SpawnFindReplaceTab( const FSpawnTabArgs& Args );
		/** Create the nested tables list tab and its content */
		TSharedRef<SDockTab> SpawnNestedTablesTreeTab( const FSpawnTabArgs& Args );
	
		TSharedRef<ITableRow> GenerateTableRow(TSharedPtr<FChooserTableRow> InItem, const TSharedRef<STableViewBase>& OwnerTable);

		/** Called when objects need to be swapped out for new versions, like after a blueprint recompile. */
		void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

		/** Details view */
		TSharedPtr< class IDetailsView > DetailsView;

		/** App Identifier. */
		static const FName ChooserEditorAppIdentifier;

		/**	The tab ids for all the tabs used */
		static const FName PropertiesTabId;
		static const FName FindReplaceTabId;
		static const FName TableTabId;
		static const FName NestedTablesTreeTabId;
		
		void AddHistory();
		bool CanNavigateBack() const;
		void NavigateBack();
		bool CanNavigateForward() const;
		void NavigateForward();

		/** The root chooser asset being edited in this editor */
		UChooserTable* RootChooser = nullptr;

		UChooserColumnDetails* SelectedColumn = nullptr;
		TArray<TObjectPtr<UChooserRowDetails>> SelectedRows;

		TSharedPtr<SBreadcrumbTrail<UChooserTable*>> BreadcrumbTrail;
		TRingBuffer<UChooserTable*> History;
		int32 HistoryIndex = 0;
		
		void UpdateTableColumns();
		TArray<TSharedPtr<FChooserTableRow>> TableRows;
	
		TSharedPtr<SPositiveActionButton> CreateColumnComboButton;
		TSharedPtr<SWidget> CreateRowComboButton;

		TSharedPtr<SHeaderRow> HeaderRow;
		TSharedPtr<SListView<TSharedPtr<FChooserTableRow>>> TableView;

		ESelectionType CurrentSelectionType = ESelectionType::Root;

		TSharedRef<SWidget>	MakeCreateRowMenu();
		TSharedRef<SWidget>	MakeCreateColumnMenu();
	public:

		TSharedPtr<SWidget>& GetCreateRowComboButton() { return CreateRowComboButton; };
		TSharedPtr<SNestedChooserTree> NestedChooserTree;

		/** The name given to all instances of this type of editor */
		static const FName ToolkitFName;

		static TSharedRef<FChooserTableEditor> CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit, FGetDetailsViewObjects GetDetailsViewObjects = FGetDetailsViewObjects() );

		static TSharedRef<FChooserTableEditor> CreateEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, const TArray<UObject*>& ObjectsToEdit, FGetDetailsViewObjects GetDetailsViewObjects = FGetDetailsViewObjects() );

		static void RegisterWidgets();
		
		static FName EditorName;
		static FName ContextMenuName;
	};

}

// todo: for menus to actually be extensible this needs to be somewhere public
UCLASS()
class UChooserEditorToolMenuContext : public UObject
{
	GENERATED_BODY()

public:
	TWeakPtr<UE::ChooserEditor::FChooserTableEditor> ChooserEditor;
};