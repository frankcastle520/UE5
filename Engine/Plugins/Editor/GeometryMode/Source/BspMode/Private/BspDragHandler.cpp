// Copyright Epic Games, Inc. All Rights Reserved.

#include "BspDragHandler.h"
#include "EditorClassUtils.h"
#include "BspModeModule.h"
#include "DragAndDrop/BrushBuilderDragDropOp.h"

FBspDragHandler::FBspDragHandler()
{
}

void FBspDragHandler::Initialize( TSharedRef<FBspBuilderType> BspBuilder )
{
	ToolTip = FEditorClassUtils::GetTooltip(ABrush::StaticClass(), BspBuilder->ToolTipText );
	IconBrush = BspBuilder->Icon; 
	
	GetContentToDrag.BindLambda( [ BspBuilder ] ()
	{
		const bool bIsAdditive = true;
		
		const TWeakObjectPtr<UBrushBuilder> ActiveBrushBuilder = GEditor->FindBrushBuilder(BspBuilder->BuilderClass.Get());
		return FBrushBuilderDragDropOp::New( ActiveBrushBuilder, BspBuilder->Icon, bIsAdditive );
	});
}