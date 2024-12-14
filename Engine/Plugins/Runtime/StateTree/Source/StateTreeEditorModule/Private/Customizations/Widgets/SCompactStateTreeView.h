// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Textures/SlateIcon.h"
#include "UObject/ObjectKey.h"
#include "Misc/Guid.h"

class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
class SSearchBox;
class FStateTreeViewModel;

/**
 * Widget that displays a list of State Tree nodes which match base types and specified schema.
 * Can be used e.g. in popup menus to select node types.
 */
class SCompactStateTreeView : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, TConstArrayView<FGuid> /*SelectedStateIDs*/);

	SLATE_BEGIN_ARGS(SCompactStateTreeView)
		: _StateTreeEditorData(nullptr)
		, _SelectionMode(ESelectionMode::Single)
		, _SelectableStatesOnly(false)
		, _SubtreesOnly(false)
	{}
		/** Currently selected struct, initially highlighted. */
		SLATE_ARGUMENT(const UStateTreeEditorData*, StateTreeEditorData)
		SLATE_ARGUMENT(ESelectionMode::Type, SelectionMode)
		SLATE_ARGUMENT(bool, SelectableStatesOnly)
		SLATE_ARGUMENT(bool, SubtreesOnly)
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)
		SLATE_EVENT(FOnContextMenuOpening, OnContextMenuOpening)
	SLATE_END_ARGS()

	SCompactStateTreeView();
	virtual ~SCompactStateTreeView() override;

	void Construct(const FArguments& InArgs, TSharedPtr<FStateTreeViewModel> InViewModel = nullptr);

	/** @returns widget to focus (search box) when the picker is opened. */
	TSharedPtr<SWidget> GetWidgetToFocusOnOpen();

	void SetSelection(TConstArrayView<FGuid> Selection);
	TArray<FGuid> GetSelection() const;

	void Refresh(const UStateTreeEditorData* NewStateTreeEditorData = nullptr);
	
private:
	// Stores info about state
	struct FStateTreeStateItem : TSharedFromThis<FStateTreeStateItem>
	{
		FStateTreeStateItem() = default;
		
		FStateTreeStateItem(const FText& InDesc, const FText& InTooltipText, const FSlateBrush* InIcon)
			: Desc(InDesc)
			, TooltipText(InTooltipText)
			, Icon(InIcon)
		{
		}

		FText Desc;
		FText TooltipText;
		FGuid StateID = {};
		FSlateColor Color = FSlateColor(FLinearColor::White);
		const FSlateBrush* Icon = nullptr;
		bool bIsSubTree = false;
		bool bIsLinked = false;
		bool bIsEnabled = true;
		FText LinkedDesc;
		TArray<TSharedPtr<FStateTreeStateItem>> Children;
	};

	// Stores per session node expansion state for a node type.
	struct FStateExpansionState
	{
		TSet<FGuid> CollapsedStates;
	};

	void CacheStates();
	void CacheState(TSharedPtr<FStateTreeStateItem> ParentNode, const UStateTreeState* State);

	TSharedRef<ITableRow> GenerateStateItemRow(TSharedPtr<FStateTreeStateItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void GetStateItemChildren(TSharedPtr<FStateTreeStateItem> Item, TArray<TSharedPtr<FStateTreeStateItem>>& OutItems) const;
	void OnStateItemSelected(TSharedPtr<FStateTreeStateItem> SelectedItem, ESelectInfo::Type);
	void OnStateItemExpansionChanged(TSharedPtr<FStateTreeStateItem> ExpandedItem, bool bInExpanded) const;
	void OnSearchBoxTextChanged(const FText& NewText);
	void UpdateFilteredRoot(bool bRestoreSelection = true);
	static int32 FilterStateItemChildren(const TArray<FString>& FilterStrings, const bool bParentMatches, const TArray<TSharedPtr<FStateTreeStateItem>>& SourceArray, TArray<TSharedPtr<FStateTreeStateItem>>& OutDestArray);
	void ExpandAll(const TArray<TSharedPtr<FStateTreeStateItem>>& Items);
	
	static bool FindStateByIDRecursive(const TSharedPtr<FStateTreeStateItem>& Item, const FGuid StateID, TArray<TSharedPtr<FStateTreeStateItem>>& OutPath);
	static void FindStatesByIDRecursive(const TSharedPtr<FStateTreeStateItem>& Item, TConstArrayView<FGuid> StateIDs, TArray<TSharedPtr<FStateTreeStateItem>>& OutStates);
	
	void RestoreExpansionState();

	FReply HandleDragDetected(const FGeometry&, const FPointerEvent&) const;
	void HandleDragLeave(const FDragDropEvent& DragDropEvent) const;
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FStateTreeStateItem> TargetState) const;
	FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FStateTreeStateItem> TargetState) const;

	TSharedPtr<FStateTreeStateItem> RootItem;
	TSharedPtr<FStateTreeStateItem> FilteredRootItem;

	TSharedPtr<SSearchBox> SearchBox;
	
	TSharedPtr<STreeView<TSharedPtr<FStateTreeStateItem>>> StateItemTree;
	bool bIsRestoringExpansion = false;
	TWeakObjectPtr<const UStateTreeEditorData> WeakStateTreeEditorData = nullptr;

	TArray<FString> FilterStrings;
	
	// If set, allow to select only states marked as subtrees.
	bool bSubtreesOnly = false;

	bool bSelectableStatesOnly = false;

	bool bIsSettingSelection = false;

	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
	
	FOnSelectionChanged OnSelectionChanged;
	FOnContextMenuOpening OnContextMenuOpening;

	// Save expansion state for each base node type. The expansion state does not persist between editor sessions. 
	static TMap<FObjectKey, FStateExpansionState> StateExpansionStates;
};