// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "SDMXControlConsoleEditorCueList.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

class FReply;
struct FSlateBrush;
class SInlineEditableTextBlock;


namespace UE::DMX::Private
{
	class FDMXControlConsoleCueStackModel;

	DECLARE_DELEGATE_OneParam(FDMXControleConsolEditorCueListItemDelegate, TSharedPtr<FDMXControlConsoleEditorCueListItem>)
	DECLARE_DELEGATE_TwoParams(FDMXControleConsolEditorMoveCueListItemDelegate, TSharedPtr<FDMXControlConsoleEditorCueListItem>, EItemDropZone)

	/** A Control Console Cue as a row in a list */
	class SDMXControlConsoleEditorCueListRow
		: public SMultiColumnTableRow<TSharedPtr<FDMXControlConsoleEditorCueListItem>>
	{
	public:
		SLATE_BEGIN_ARGS(SDMXControlConsoleEditorCueListRow)
			{}

			/** Executed when the color of a cue list item is edited */
			SLATE_EVENT(FDMXControleConsolEditorCueListItemDelegate, OnEditCueItemColor)

			/** Executed when a cue list item is renamed */
			SLATE_EVENT(FDMXControleConsolEditorCueListItemDelegate, OnRenameCueItem)

			/** Executed when a cue list item is moved in a new position */
			SLATE_EVENT(FDMXControleConsolEditorMoveCueListItemDelegate, OnMoveCueItem)

			/** Executed when a cue list item is deleted */
			SLATE_EVENT(FDMXControleConsolEditorCueListItemDelegate, OnDeleteCueItem)

			/** Executed when a row was dragged */
			SLATE_EVENT(FOnDragDetected, OnDragDetected)

			/** Executed when the row drop operation needs to be accepted */
			SLATE_EVENT(FOnCanAcceptDrop, OnCanAcceptDrop)

			/** Executed when the row drop operation is accepted */
			SLATE_EVENT(FOnAcceptDrop, OnAcceptDrop)

		SLATE_END_ARGS()

		/** Constructs this widget */
		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TSharedRef<FDMXControlConsoleEditorCueListItem>& InItem, TSharedPtr<FDMXControlConsoleCueStackModel> InCueStackModel);

	protected:
		//~ Begin SMultiColumnTableRow interface
		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
		//~ End SMultiColumnTableRow interface

	private:
		/** Generates the row that displays the color of the Cue */
		TSharedRef<SWidget> GenerateCueColorRow();

		/** Generates the row that displays the recall state of the Cue */
		TSharedRef<SWidget> GenerateCueStateRow();

		/** Generates the row that displays the name label of the Cue */
		TSharedRef<SWidget> GenerateCueNameRow();

		/** Generates the row that displays the edit options for the Cue */
		TSharedRef<SWidget> GenerateCueOptionsRow();

		/** Generates a row option button with the given parameters */
		TSharedRef<SWidget> GenerateRowOptionButtonWidget(const FSlateBrush* IconBrush, FOnClicked OnClicked);

		/** Called when the color section of this row is clicked */
		FReply OnCueColorMouseButtonClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

		/** Called when the color picker value is committed */
		void OnSetCueColorFromColorPicker(FLinearColor NewColor);

		/** Gets the current cue name as text */
		FText GetCueNameAsText() const;

		/** Called when the text in the cue name text box is committed */
		void OnCueNameTextCommitted(const FText& NewName, ETextCommit::Type InCommit);

		/** Called to enter the edit mode of the cue label text block */
		void OnEnterCueLabelTextBlockEditMode();

		/** Called when the rename button is clicked */
		FReply OnRenameItemClicked();

		/** Called when the move button is clicked */
		FReply OnMoveItemClicked(EItemDropZone DropZone);

		/** Called when the delete button is clicked */
		FReply OnDeleteItemClicked();

		/** Gets the visibility of the recalled cue tag */
		EVisibility GetRecalledCueTagVisibility() const;

		/** Timer handle in use while entering cue label edit mode is requested but not carried out yet */
		FTimerHandle EnterCueLabelTextBlockEditModeTimerHandle;

		/** The editable text block that shows the name label of the cue item this row is based on */
		TSharedPtr<SInlineEditableTextBlock> CueLabelEditableTextBlock;

		/** The item this widget draws */
		TSharedPtr<FDMXControlConsoleEditorCueListItem> Item;

		/** Weak reference to the Control Console Cue Stack Model */
		TWeakPtr<FDMXControlConsoleCueStackModel> WeakCueStackModel;

		// Slate Arguments
		FDMXControleConsolEditorCueListItemDelegate OnEditCueItemColorDelegate;
		FDMXControleConsolEditorCueListItemDelegate OnRenameCueItemDelegate;
		FDMXControleConsolEditorMoveCueListItemDelegate OnMoveCueItemDelegate;
		FDMXControleConsolEditorCueListItemDelegate OnDeleteCueItemDelegate;
	};
}
