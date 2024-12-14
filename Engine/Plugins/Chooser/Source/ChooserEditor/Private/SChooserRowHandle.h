// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DragAndDrop/DecoratedDragDropOp.h"

class UChooserTable;

namespace UE::ChooserEditor
{

class FChooserTableEditor;

class FChooserRowDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FChooserRowDragDropOp, FDecoratedDragDropOp)

	UChooserTable* RowData = nullptr;
	int32 TransactionIndex = -1;
	FChooserTableEditor* Editor = nullptr;

	/** Constructs the drag drop operation */
	static TSharedRef<FChooserRowDragDropOp> New(FChooserTableEditor* InEditor, uint32 InRowIndex);

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;
};

class SChooserRowHandle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChooserRowHandle)
	{}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_ARGUMENT(FChooserTableEditor*, ChooserEditor)
	SLATE_ARGUMENT(uint32, RowIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, bool bShowImage);
	
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FChooserTableEditor* ChooserEditor = nullptr;
	uint32 RowIndex = 0;
};

}
