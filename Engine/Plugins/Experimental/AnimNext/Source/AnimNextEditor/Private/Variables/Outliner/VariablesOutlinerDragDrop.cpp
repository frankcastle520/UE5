// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerDragDrop.h"

#include "AnimNextEdGraph.h"
#include "Common/GraphEditorSchemaActions.h"

#define LOCTEXT_NAMESPACE "VariableDragDropOp"

namespace UE::AnimNext::Editor
{

TSharedPtr<FVariableDragDropOp> FVariableDragDropOp::New(TSharedPtr<FAnimNextSchemaAction_Variable> InAction)
{
	TSharedPtr<FVariableDragDropOp> NewOp = MakeShared<FVariableDragDropOp>();
	NewOp->SourceAction = InAction;
	NewOp->Construct();
	return NewOp;
}

void FVariableDragDropOp::GetDefaultStatusSymbol(const FSlateBrush*& PrimaryBrushOut, FSlateColor& IconColorOut, FSlateBrush const*& SecondaryBrushOut, FSlateColor& SecondaryColorOut) const
{
	PrimaryBrushOut = StaticCastSharedPtr<FAnimNextSchemaAction_Variable>(SourceAction)->GetIconBrush();
	IconColorOut = StaticCastSharedPtr<FAnimNextSchemaAction_Variable>(SourceAction)->GetIconColor();
	SecondaryBrushOut = nullptr;
	SecondaryColorOut = FSlateColor::UseForeground();
}

}

#undef LOCTEXT_NAMESPACE