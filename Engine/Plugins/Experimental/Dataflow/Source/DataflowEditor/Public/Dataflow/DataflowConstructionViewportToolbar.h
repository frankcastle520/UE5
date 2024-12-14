// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"

// ----------------------------------------------------------------------------------
class SDataflowConstructionViewport;
class SEditorViewportToolbarMenu;

/** Base toolbar for the dataflow. Should be extended to add more features */
class SDataflowConstructionViewportSelectionToolBar : public SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SDataflowConstructionViewportSelectionToolBar) {}
		SLATE_ARGUMENT(TSharedPtr<class FUICommandList>, CommandList)
		SLATE_ARGUMENT(TSharedPtr<class FExtender>, Extenders)
	SLATE_END_ARGS()

	/** Constructs this widget with the given parameters */
	void Construct(const FArguments& InArgs, TSharedPtr<SDataflowConstructionViewport> InDataflowViewport);


private:

	// SCommonEditorViewportToolbarBase
	virtual TSharedRef<SWidget> GenerateShowMenu() const override;

	TSharedRef<SWidget> MakeToolBar(const TSharedPtr<FExtender> InExtenders);

	// View mode
	FText GetViewModeMenuLabel() const;
	const FSlateBrush* GetViewModeMenuLabelIcon() const;
	TSharedRef<SWidget> GenerateViewModeMenuContent() const;

	// Display (Lit, Unlit, Wireframe, etc.)
	TSharedRef<SWidget> MakeDisplayToolBar(const TSharedPtr<FExtender> InExtenders);

	TSharedPtr<SEditorViewportToolbarMenu> ViewModeDropDown;

	/** Reference to the parent viewport */
	TWeakPtr<SDataflowConstructionViewport> EditorViewport;

	TSharedPtr<FUICommandList> CommandList;
};