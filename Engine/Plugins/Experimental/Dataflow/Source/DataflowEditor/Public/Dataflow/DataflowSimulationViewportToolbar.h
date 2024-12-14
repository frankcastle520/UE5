// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"

// ----------------------------------------------------------------------------------
class SDataflowSimulationViewport;
class FExtender;
class FUICommandList;

/** Base toolbar for the dataflow. Should be extended to add more features */
class SDataflowSimulationViewportToolBar : public SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SDataflowSimulationViewportToolBar) {}
		SLATE_ARGUMENT(TSharedPtr<FUICommandList>, CommandList)
		SLATE_ARGUMENT(TSharedPtr<FExtender>, Extenders)
	SLATE_END_ARGS()

	/** Constructs this widget with the given parameters */
	void Construct(const FArguments& InArgs, TSharedPtr<SDataflowSimulationViewport> InDataflowViewport);
	virtual void ExtendLeftAlignedToolbarSlots(TSharedPtr<SHorizontalBox> MainBoxPtr, TSharedPtr<SViewportToolBar> ParentToolBarPtr) const override;
	
private:
	/** Reference to the parent viewport */
	TWeakPtr<SDataflowSimulationViewport> EditorViewport;

	TSharedRef<SWidget> MakeToolBar(const TSharedPtr<FExtender> InExtenders) const;

	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr<FExtender> Extenders;
};