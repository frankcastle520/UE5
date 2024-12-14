// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorModeTools.h"
#include "EditorViewportSelectability.h"

class FSequencerSelectabilityTool : public FModeTool, public FEditorViewportSelectability
{
public:
	FSequencerSelectabilityTool() = delete;
	FSequencerSelectabilityTool(const FOnGetWorld& InOnGetWorld, const FOnIsObjectSelectableInViewport& InOnIsObjectSelectableInViewport);

	//~ Begin FModeTool
	virtual FString GetName() const override
	{
		return TEXT("Sequencer Selectability");
	}
	virtual bool BoxSelect(FBox& InBox, const bool InSelect) override;
	virtual bool FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* const InEditorViewportClient, const bool InSelect) override;
	virtual void DrawHUD(FEditorViewportClient* const InEditorViewportClient, FViewport* const InViewport, const FSceneView* InView, FCanvas* const InCanvas) override;
	//~ End FModeTool
};
