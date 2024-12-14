// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXControlConsoleCueStack.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"

enum class EItemDropZone;
struct FDMXControlConsoleCue;
class FDMXControlConsoleEditorCueListItem;
class ITableRow;
class SHeaderRow;
template <typename ItemType> class SListView;
class STableViewBase;


namespace UE::DMX::Private
{
	class FDMXControlConsoleCueStackModel;

	/** Collumn ids in the cue list */
	struct FDMXControlConsoleEditorCueListColumnIDs
	{
		static const FName Color;
		static const FName State;
		static const FName Name;
		static const FName Options;
	};

	/** An item in the Control Console cue list */
	class FDMXControlConsoleEditorCueListItem
		: public TSharedFromThis<FDMXControlConsoleEditorCueListItem>
	{
	public:
		/** Constructor */
		FDMXControlConsoleEditorCueListItem(const FDMXControlConsoleCue& InCue);

		/** Returns the cue this item uses */
		FDMXControlConsoleCue GetCue() const { return Cue; }

		/** Returns the name label of the cue this item uses, as text */
		FText GetCueNameText() const;

		/** Sets the name label of the cue this item uses */
		void SetCueName(const FString& CueLabel);

		/** Returns the color of the cue this item uses */
		FSlateColor GetCueColor() const;

		/** Sets the color of the cue this item uses */
		void SetCueColor(const FLinearColor CueColor);

	private:
		/** The cue this item is based on */
		FDMXControlConsoleCue Cue;
	};

	/** This drag drop operation allows cues from the cue stack to be rearranged */
	class FDMXControlConsoleEditorCueListDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FDMXControlConsoleEditorCueListDragDropOp, FDecoratedDragDropOp)

		/** Constructs the drag drop operation. */
		static TSharedRef<FDMXControlConsoleEditorCueListDragDropOp> New(TWeakPtr<FDMXControlConsoleEditorCueListItem> InItem);

		//~ Begin FDecoratedDragDropOp interface
		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
		//~ End FDecoratedDragDropOp interface

		/** The item dragged by this operation */
		TWeakPtr<FDMXControlConsoleEditorCueListItem> CueItem;
	};

	/** List of Cues in a DMX Control Console */
	class SDMXControlConsoleEditorCueList
		: public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SDMXControlConsoleEditorCueList)
			{}

		SLATE_END_ARGS()

		/** Constructs this widget */
		void Construct(const FArguments& InArgs, TSharedPtr<FDMXControlConsoleCueStackModel> InCueStackModel);

		/** Gets the array of current selected cue items */
		TArray<TSharedPtr<FDMXControlConsoleEditorCueListItem>> GetSelectedCueItems() const;

		/** Requests the refresh of the list */
		void RequestRefresh();

	private:
		/** Updates the array of Cue List Items */
		void UpdateCueListItems();

		/** Called to generate the header row of the list */
		TSharedRef<SHeaderRow> GenerateHeaderRow();

		/** Called to generate a row in the list */
		TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDMXControlConsoleEditorCueListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

		/** Called when selection in the list changed */
		void OnSelectionChanged(const TSharedPtr<FDMXControlConsoleEditorCueListItem> NewSelection, ESelectInfo::Type SelectInfo);

		/** Called when a row was double clicked */
		void OnRowDoubleClicked(const TSharedPtr<FDMXControlConsoleEditorCueListItem> ItemClicked);

		/** Called when a row in the list was dragged */
		FReply OnRowDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

		/** Called when the row drop operation needs to be accepted */
		TOptional<EItemDropZone> OnRowCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FDMXControlConsoleEditorCueListItem> TargetItem);
		
		/** Called when the row drop operation is accepted */
		FReply OnRowAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FDMXControlConsoleEditorCueListItem> TargetItem);

		/** Called when the color of an item in the list is changed */
		void OnEditCueItemColor(TSharedPtr<FDMXControlConsoleEditorCueListItem> InItem);

		/** Called when the name label of an item in the list is changed */
		void OnRenameCueItem(TSharedPtr<FDMXControlConsoleEditorCueListItem> InItem);

		/** Called when an item in the list is deleted */
		void OnMoveCueItem(TSharedPtr<FDMXControlConsoleEditorCueListItem> InItem, EItemDropZone DropZone);

		/** Called when an item in the list is deleted */
		void OnDeleteCueItem(TSharedPtr<FDMXControlConsoleEditorCueListItem> InItem);

		/** Reference to the Cue List View widget */
		TSharedPtr<SListView<TSharedPtr<FDMXControlConsoleEditorCueListItem>>> CueListView;

		/** The array of Cue List Items this list is based on */
		TArray<TSharedPtr<FDMXControlConsoleEditorCueListItem>> CueListItems;

		/** The last selected control console cue */
		FDMXControlConsoleCue LastSelectedCue;

		/** Weak reference to the Control Console Cue Stack Model */
		TWeakPtr<FDMXControlConsoleCueStackModel> WeakCueStackModel;
	};
}
