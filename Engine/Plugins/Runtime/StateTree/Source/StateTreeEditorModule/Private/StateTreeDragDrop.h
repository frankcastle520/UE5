// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DragAndDrop/DecoratedDragDropOp.h"

class FStateTreeViewModel;

class FStateTreeSelectedDragDrop : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FActionTreeViewDragDrop, FDecoratedDragDropOp);

	static TSharedRef<FStateTreeSelectedDragDrop> New(TSharedPtr<FStateTreeViewModel> InViewModel)
	{
		TSharedRef<FStateTreeSelectedDragDrop> Operation = MakeShared<FStateTreeSelectedDragDrop>();
		Operation->ViewModel = InViewModel;
		Operation->Construct();

		return Operation;
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	void SetCanDrop(const bool bState)
	{
		bCanDrop = bState;
	}

	TSharedPtr<FStateTreeViewModel> ViewModel;
	bool bCanDrop = false;
};