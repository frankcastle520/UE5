// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCompactStateTreeView.h"

#include "StateTreeDelegates.h"
#include "StateTreeDragDrop.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorStyle.h"
#include "StateTreeViewModel.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "TextStyleDecorator.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TMap<FObjectKey, SCompactStateTreeView::FStateExpansionState> SCompactStateTreeView::StateExpansionStates;

SCompactStateTreeView::SCompactStateTreeView()
{
}

SCompactStateTreeView::~SCompactStateTreeView()
{
}

void SCompactStateTreeView::Construct(const SCompactStateTreeView::FArguments& InArgs, TSharedPtr<FStateTreeViewModel> InViewModel)
{
	StateTreeViewModel = InViewModel;
	WeakStateTreeEditorData = InArgs._StateTreeEditorData;
	bSubtreesOnly = InArgs._SubtreesOnly;
	bSelectableStatesOnly = InArgs._SelectableStatesOnly;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	OnContextMenuOpening = InArgs._OnContextMenuOpening;
	
	CacheStates();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(4, 2, 4, 2)
		.AutoHeight()
		[
			SAssignNew(SearchBox, SSearchBox)
			.OnTextChanged(this, &SCompactStateTreeView::OnSearchBoxTextChanged)
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(StateItemTree, STreeView<TSharedPtr<FStateTreeStateItem>>)
			.SelectionMode(InArgs._SelectionMode)
			.TreeItemsSource(&FilteredRootItem->Children)
			.OnGenerateRow(this, &SCompactStateTreeView::GenerateStateItemRow)
			.OnGetChildren(this, &SCompactStateTreeView::GetStateItemChildren)
			.OnSelectionChanged(this, &SCompactStateTreeView::OnStateItemSelected)
			.OnExpansionChanged(this, &SCompactStateTreeView::OnStateItemExpansionChanged)
			.OnContextMenuOpening_Lambda([this]()
			{
				if (OnContextMenuOpening.IsBound())
				{
					return OnContextMenuOpening.Execute();
				}
				return SNullWidget::NullWidget.ToSharedPtr();
			})
		]
	];

	// Restore category expansion state from previous use.
	RestoreExpansionState();
}

void SCompactStateTreeView::Refresh(const UStateTreeEditorData* NewStateTreeEditorData)
{
	if (!StateItemTree)
	{
		return;
	}

	if (NewStateTreeEditorData)
	{
		WeakStateTreeEditorData = NewStateTreeEditorData;
	}

	TArray<FGuid> SelectedItemIDs = GetSelection();
	
	CacheStates();
	UpdateFilteredRoot(/*bRestoreSelection*/false);

	SetSelection(SelectedItemIDs);
}

void SCompactStateTreeView::SetSelection(TConstArrayView<FGuid> Selection)
{
	if (!StateItemTree)
	{
		return;
	}
	
	if (bIsSettingSelection)
	{
		return;
	}
	
	TArray<TSharedPtr<FStateTreeStateItem>> SelectedStates;
	FindStatesByIDRecursive(FilteredRootItem, Selection, SelectedStates);

	bIsSettingSelection = true;
	
	StateItemTree->ClearSelection();
	StateItemTree->SetItemSelection(SelectedStates, true);

	if (SelectedStates.Num() == 1)
	{
		StateItemTree->RequestScrollIntoView(SelectedStates[0]);	
	}

	bIsSettingSelection = false;
}

TArray<FGuid> SCompactStateTreeView::GetSelection() const
{
	TArray<FGuid> SelectedItemIDs;
	
	TArray<TSharedPtr<FStateTreeStateItem>> SelectedItems = StateItemTree->GetSelectedItems();
	for (const TSharedPtr<FStateTreeStateItem>& Item : SelectedItems)
	{
		if (Item)
		{
			SelectedItemIDs.Add(Item->StateID);
		}
	}

	return SelectedItemIDs;
}

TSharedPtr<SWidget> SCompactStateTreeView::GetWidgetToFocusOnOpen()
{
	return SearchBox;
}

void SCompactStateTreeView::CacheStates()
{
	RootItem = MakeShared<FStateTreeStateItem>();

	if (const UStateTreeEditorData* StateTreeEditorData = WeakStateTreeEditorData.Get())
	{
		for (const UStateTreeState* SubTree : StateTreeEditorData->SubTrees)
		{
			CacheState(RootItem, SubTree);
		}
	}

	FilteredRootItem = RootItem;
}

void SCompactStateTreeView::CacheState(TSharedPtr<FStateTreeStateItem> ParentNode, const UStateTreeState* State)
{
	if (!State)
	{
		return;
	}
	const UStateTreeEditorData* StateTreeEditorData = WeakStateTreeEditorData.Get();
	if (!StateTreeEditorData)
	{
		return;
	}

	bool bShouldAdd = true;
	if (bSubtreesOnly
		&& State->Type != EStateTreeStateType::Subtree)
	{
		bShouldAdd = false;
	}

	if (bSelectableStatesOnly
		&& State->SelectionBehavior == EStateTreeStateSelectionBehavior::None)
	{
		bShouldAdd = false;
	}
	
	if (bShouldAdd)
	{
		TSharedRef<FStateTreeStateItem> StateItem = MakeShared<FStateTreeStateItem>();
		StateItem->Desc = FText::FromName(State->Name);
		StateItem->StateID = State->ID;
		StateItem->bIsSubTree = State->Type == EStateTreeStateType::Subtree;
		StateItem->bIsEnabled = State->bEnabled;

		StateItem->Color = FColor(31, 151, 167);
		if (const FStateTreeEditorColor* FoundColor = StateTreeEditorData->FindColor(State->ColorRef))
		{
			StateItem->Color = FoundColor->Color;
		}
		
		StateItem->Icon = FStateTreeEditorStyle::GetBrushForSelectionBehaviorType(State->SelectionBehavior, !State->Children.IsEmpty(), State->Type);

		// Linked states
		if (State->Type == EStateTreeStateType::Linked)
		{
			StateItem->bIsLinked = true;
			StateItem->LinkedDesc = FText::FromName(State->LinkedSubtree.Name);
		}
		else if (State->Type == EStateTreeStateType::LinkedAsset)
		{
			StateItem->bIsLinked = true;
			StateItem->LinkedDesc = FText::FromString(GetNameSafe(State->LinkedAsset.Get()));
		}
		
		ParentNode->Children.Add(StateItem);

		ParentNode = StateItem;
	}

	for (const UStateTreeState* ChildState : State->Children)
	{
		CacheState(ParentNode, ChildState);
	}
}

TSharedRef<ITableRow> SCompactStateTreeView::GenerateStateItemRow(TSharedPtr<FStateTreeStateItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SHorizontalBox> Container = SNew(SHorizontalBox);

	// Icon
	Container->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(0, 2.0f, 4.0f, 2.0f)
		.AutoWidth()
		[
			SNew(SImage)
			.Visibility(Item->Icon ? EVisibility::Visible : EVisibility::Collapsed)
			.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
			.Image(Item->Icon)
			.ColorAndOpacity(Item->Color)
			.IsEnabled(Item->bIsEnabled)
		];

	// Name
	Container->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SRichTextBlock)
			.Text(Item->Desc)
			.TextStyle(&FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Normal.Normal"))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.ToolTipText(Item->TooltipText)
			.IsEnabled(Item->bIsEnabled)
			.HighlightText_Lambda([this]() { return SearchBox.IsValid() ? SearchBox->GetText() : FText::GetEmpty(); })
			+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT(""), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Normal.Normal")))
			+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT("b"), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Normal.Bold")))
			+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT("i"), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Normal.Italic")))
			+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT("s"), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Normal.Subdued")))
		];

	// Link
	if (Item->bIsLinked)
	{
		// Link icon
		Container->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f)
			.AutoWidth()
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Image(FStateTreeEditorStyle::Get().GetBrush("StateTreeEditor.StateLinked"))
		];

		// Linked name
		Container->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.NormalFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(Item->LinkedDesc)
			];
	}
	
	return SNew(STableRow<TSharedPtr<FStateTreeStateItem>>, OwnerTable)
		.OnDragDetected(this, &SCompactStateTreeView::HandleDragDetected)
		.OnDragLeave(this, &SCompactStateTreeView::HandleDragLeave)
		.OnCanAcceptDrop(this, &SCompactStateTreeView::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SCompactStateTreeView::HandleAcceptDrop)
		[
			Container
		];
}

void SCompactStateTreeView::GetStateItemChildren(TSharedPtr<FStateTreeStateItem> Item, TArray<TSharedPtr<FStateTreeStateItem>>& OutItems) const
{
	if (Item.IsValid())
	{
		OutItems = Item->Children;
	}
}

void SCompactStateTreeView::OnStateItemSelected(TSharedPtr<FStateTreeStateItem> SelectedItem, ESelectInfo::Type Type)
{
	// Skip selection from code
	if (Type == ESelectInfo::Direct)
	{
		return;
	}

	if (bIsSettingSelection)
	{
		return;
	}

	if (OnSelectionChanged.IsBound())
	{
		TArray<FGuid> SelectedStateIDs;
		
		TArray<TSharedPtr<FStateTreeStateItem>> Selection = StateItemTree->GetSelectedItems();
		for (const TSharedPtr<FStateTreeStateItem>& Item : Selection)
		{
			if (Item)
			{
				SelectedStateIDs.Add(Item->StateID);
			}
		}

		OnSelectionChanged.Execute(SelectedStateIDs);
	}
}

void SCompactStateTreeView::OnStateItemExpansionChanged(TSharedPtr<FStateTreeStateItem> ExpandedItem, bool bInExpanded) const
{
	// Do not save expansion state when restoring expansion state, or when showing filtered results. 
	if (bIsRestoringExpansion || FilteredRootItem != RootItem)
	{
		return;
	}

	if (ExpandedItem.IsValid() && ExpandedItem->StateID.IsValid())
	{
		FStateExpansionState& ExpansionState = StateExpansionStates.FindOrAdd(FObjectKey(WeakStateTreeEditorData.Get()));
		if (bInExpanded)
		{
			ExpansionState.CollapsedStates.Remove(ExpandedItem->StateID);
		}
		else
		{
			ExpansionState.CollapsedStates.Add(ExpandedItem->StateID);
		}
	}
}

void SCompactStateTreeView::OnSearchBoxTextChanged(const FText& NewText)
{
	if (!StateItemTree.IsValid())
	{
		return;
	}
	

	NewText.ToString().ParseIntoArrayWS(FilterStrings);
	FilterStrings.RemoveAll([](const FString& String) { return String.IsEmpty(); });

	UpdateFilteredRoot();
}

void SCompactStateTreeView::UpdateFilteredRoot(bool bRestoreSelection)
{
	FilteredRootItem.Reset();

	TArray<FGuid> Selection;
	if (bRestoreSelection)
	{
		Selection = GetSelection();
	}

	if (FilterStrings.IsEmpty())
	{
		// Show all when there's no filter string.
		FilteredRootItem = RootItem;
		StateItemTree->SetTreeItemsSource(&FilteredRootItem->Children);
		RestoreExpansionState();
	}
	else
	{
		FilteredRootItem = MakeShared<FStateTreeStateItem>();
		FilterStateItemChildren(FilterStrings, /*bParentMatches*/false, RootItem->Children, FilteredRootItem->Children);

		StateItemTree->SetTreeItemsSource(&FilteredRootItem->Children);
		ExpandAll(FilteredRootItem->Children);
	}

	if (bRestoreSelection)
	{
		SetSelection(Selection);
	}

	StateItemTree->RequestTreeRefresh();
}

int32 SCompactStateTreeView::FilterStateItemChildren(const TArray<FString>& FilterStrings, const bool bParentMatches, const TArray<TSharedPtr<FStateTreeStateItem>>& SourceArray, TArray<TSharedPtr<FStateTreeStateItem>>& OutDestArray)
{
	int32 NumFound = 0;

	auto MatchFilter = [&FilterStrings](const TSharedPtr<FStateTreeStateItem>& SourceItem)
	{
		const FString ItemName = SourceItem->Desc.ToString();
		for (const FString& Filter : FilterStrings)
		{
			if (ItemName.Contains(Filter))
			{
				return true;
			}
		}
		return false;
	};

	for (const TSharedPtr<FStateTreeStateItem>& SourceItem : SourceArray)
	{
		// Check if our name matches the filters
		// If bParentMatches is true, the search matched a parent category.
		const bool bMatchesFilters = bParentMatches || MatchFilter(SourceItem);

		int32 NumChildren = 0;
		if (bMatchesFilters)
		{
			NumChildren++;
		}

		// if we don't match, then we still want to check all our children
		TArray<TSharedPtr<FStateTreeStateItem>> FilteredChildren;
		NumChildren += FilterStateItemChildren(FilterStrings, bMatchesFilters, SourceItem->Children, FilteredChildren);

		// then add this item to the destination array
		if (NumChildren > 0)
		{
			TSharedPtr<FStateTreeStateItem>& NewItem = OutDestArray.Add_GetRef(MakeShared<FStateTreeStateItem>());
			*NewItem = *SourceItem;
			NewItem->Children = FilteredChildren;

			NumFound += NumChildren;
		}
	}

	return NumFound;
}

void SCompactStateTreeView::ExpandAll(const TArray<TSharedPtr<FStateTreeStateItem>>& Items)
{
	for (const TSharedPtr<FStateTreeStateItem>& Item : Items)
	{
		StateItemTree->SetItemExpansion(Item, true);
		ExpandAll(Item->Children);
	}
}
	
bool SCompactStateTreeView::FindStateByIDRecursive(const TSharedPtr<FStateTreeStateItem>& Item, const FGuid StateID, TArray<TSharedPtr<FStateTreeStateItem>>& OutPath)
{
	OutPath.Push(Item);

	if (Item->StateID == StateID)
	{
		return true;
	}

	for (const TSharedPtr<FStateTreeStateItem>& ChildItem : Item->Children)
	{
		if (FindStateByIDRecursive(ChildItem, StateID, OutPath))
		{
			return true;
		}
	}

	OutPath.Pop();

	return false;
}

void SCompactStateTreeView::FindStatesByIDRecursive(const TSharedPtr<FStateTreeStateItem>& Item, TConstArrayView<FGuid> StateIDs, TArray<TSharedPtr<FStateTreeStateItem>>& OutStates)
{
	if (StateIDs.Contains(Item->StateID))
	{
		OutStates.Add(Item);
	}

	for (const TSharedPtr<FStateTreeStateItem>& ChildItem : Item->Children)
	{
		FindStatesByIDRecursive(ChildItem, StateIDs, OutStates);
	}
}


void SCompactStateTreeView::RestoreExpansionState()
{
	if (!StateItemTree.IsValid())
	{
		return;
	}
	
	FStateExpansionState& ExpansionState = StateExpansionStates.FindOrAdd(FObjectKey(WeakStateTreeEditorData.Get()));

	bIsRestoringExpansion = true;

	// Default state is expanded.
	ExpandAll(FilteredRootItem->Children);

	// Collapse the ones that are specifically collapsed.
	for (const FGuid& StateID : ExpansionState.CollapsedStates)
	{
		TArray<TSharedPtr<FStateTreeStateItem>> Path;
		if (FindStateByIDRecursive(FilteredRootItem, StateID, Path))
		{
			StateItemTree->SetItemExpansion(Path.Last(), false);
		}
	}

	bIsRestoringExpansion = false;
}

FReply SCompactStateTreeView::HandleDragDetected(const FGeometry&, const FPointerEvent&) const
{
	return FReply::Handled().BeginDragDrop(FStateTreeSelectedDragDrop::New(StateTreeViewModel));
}

void SCompactStateTreeView::HandleDragLeave(const FDragDropEvent& DragDropEvent) const
{
	const TSharedPtr<FStateTreeSelectedDragDrop> DragDropOperation = DragDropEvent.GetOperationAs<FStateTreeSelectedDragDrop>();
	if (DragDropOperation.IsValid())
	{
		DragDropOperation->SetCanDrop(false);
	}
}

TOptional<EItemDropZone> SCompactStateTreeView::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FStateTreeStateItem> TargetState) const
{
	if (StateTreeViewModel)
	{
		const TSharedPtr<FStateTreeSelectedDragDrop> DragDropOperation = DragDropEvent.GetOperationAs<FStateTreeSelectedDragDrop>();
		if (DragDropOperation.IsValid())
		{
			DragDropOperation->SetCanDrop(true);

			// Cannot drop on selection or child of selection.
			if (StateTreeViewModel && StateTreeViewModel->IsChildOfSelection(StateTreeViewModel->GetMutableStateByID(TargetState->StateID)))
			{
				DragDropOperation->SetCanDrop(false);
				return TOptional<EItemDropZone>();
			}

			return DropZone;
		}
	}

	return TOptional<EItemDropZone>();
}

FReply SCompactStateTreeView::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FStateTreeStateItem> TargetState) const
{
	if (StateTreeViewModel)
	{
		const TSharedPtr<FStateTreeSelectedDragDrop> DragDropOperation = DragDropEvent.GetOperationAs<FStateTreeSelectedDragDrop>();
		if (DragDropOperation.IsValid())
		{
			if (StateTreeViewModel)
			{
				if (DropZone == EItemDropZone::AboveItem)
				{
					StateTreeViewModel->MoveSelectedStatesBefore(StateTreeViewModel->GetMutableStateByID(TargetState->StateID));
				}
				else if (DropZone == EItemDropZone::BelowItem)
				{
					StateTreeViewModel->MoveSelectedStatesAfter(StateTreeViewModel->GetMutableStateByID(TargetState->StateID));
				}
				else
				{
					StateTreeViewModel->MoveSelectedStatesInto(StateTreeViewModel->GetMutableStateByID(TargetState->StateID));
				}

				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}


#undef LOCTEXT_NAMESPACE