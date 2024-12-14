// Copyright Epic Games, Inc. All Rights Reserved.

#include "SChooserRowHandle.h"
#include "ChooserTableEditor.h"
#include "Misc/ITransaction.h"

#define LOCTEXT_NAMESPACE "ChooserRowHandle"

namespace UE::ChooserEditor
{

	TSharedRef<FChooserRowDragDropOp> FChooserRowDragDropOp::New(FChooserTableEditor* InEditor, uint32 InRowIndex)
	{
		TSharedRef<FChooserRowDragDropOp> Operation = MakeShareable(new FChooserRowDragDropOp());
		Operation->DefaultHoverText = LOCTEXT("Chooser Row", "Chooser Row");
		Operation->CurrentHoverText = Operation->DefaultHoverText;

		Operation->TransactionIndex = GEditor->BeginTransaction(LOCTEXT("Drag Chooser Table Rows", "Drag Chooser Table Rows"));
		
		Operation->RowData = InEditor->CopySelectionInternal();
		InEditor->DeleteSelectedRowsInternal();
		Operation->Editor = InEditor;
		
		Operation->Construct();

		return Operation;
	}

	void FChooserRowDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
	{
		if (!bDropWasHandled)
		{
			// we need to call Apply on the global undo, or cancelling the transaction doesn't actually roll back
			GUndo->Apply();
			GEditor->CancelTransaction(TransactionIndex);

			// PostUndo doesn't get called for cancelled transactions, so refresh the editor manually
			Editor->RefreshAll();
		}
		FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
	}

	void SChooserRowHandle::Construct(const FArguments& InArgs, bool bShowImage)
	{
		ChooserEditor = InArgs._ChooserEditor;
		RowIndex = InArgs._RowIndex;

		if (bShowImage)
		{
			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox) .Padding(0.0f) .HAlign(HAlign_Center) .VAlign(VAlign_Center) .WidthOverride(16.0f)
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SBox).Padding(0.0f) .HAlign(HAlign_Center) .VAlign(VAlign_Center) .WidthOverride(16.0f)
					[
						SNew(SImage)
						.Visibility_Lambda([this]()
						{
							return ChooserEditor->GetChooser()->GetDebugTestValuesValid() && RowIndex == ChooserEditor->GetChooser()->GetDebugSelectedRow() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
						})
						.Image(FAppStyle::Get().GetBrush("Icons.ArrowRight"))
					]
				]
			];
		}
	}
	
	FReply SChooserRowHandle::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
	{
		UChooserTable* Chooser = ChooserEditor->GetChooser();

		if (const FNestedChooser* NestedChooserResult = Chooser->ResultsStructs[RowIndex].GetPtr<FNestedChooser>())
		{
			if (NestedChooserResult->Chooser)
			{
				ChooserEditor->PushChooserTableToEdit(NestedChooserResult->Chooser);
			}
		}
		return FReply::Handled();
	}

	FReply SChooserRowHandle::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		// act as a move handle if the row is already selected, and if there are multiselect modifiers pressed
		if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton && !MouseEvent.IsControlDown() && !MouseEvent.IsShiftDown()
			&& ChooserEditor->IsRowSelected(RowIndex))
		{
			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		}
		else
		{
			return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
		}
	};

	FReply SChooserRowHandle::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		TSharedRef<FChooserRowDragDropOp> DragDropOp = FChooserRowDragDropOp::New(ChooserEditor, RowIndex);
		return FReply::Handled().BeginDragDrop(DragDropOp);
	}

}

#undef LOCTEXT_NAMESPACE
