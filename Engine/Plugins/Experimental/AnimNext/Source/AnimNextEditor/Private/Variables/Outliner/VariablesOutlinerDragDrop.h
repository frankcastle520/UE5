// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GraphEditorDragDropAction.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

struct FAnimNextSchemaAction_Variable;
class UAnimNextRigVMAssetEntry;

namespace UE::AnimNext::Editor
{

class FVariableDragDropOp : public FGraphSchemaActionDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE(FVariableDragDropOp, FGraphSchemaActionDragDropAction)

	static TSharedPtr<FVariableDragDropOp> New(TSharedPtr<FAnimNextSchemaAction_Variable> InAction);

	// FGraphSchemaActionDragDropAction interface
	virtual void GetDefaultStatusSymbol(const FSlateBrush*& PrimaryBrushOut, FSlateColor& IconColorOut, FSlateBrush const*& SecondaryBrushOut, FSlateColor& SecondaryColorOut) const override;
};

}
