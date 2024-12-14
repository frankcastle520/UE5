// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerSelectabilityTool.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "SequencerCommands.h"

#define LOCTEXT_NAMESPACE "SequencerSelectabilityTool"

FSequencerSelectabilityTool::FSequencerSelectabilityTool(const FOnGetWorld& InOnGetWorld, const FOnIsObjectSelectableInViewport& InOnIsObjectSelectableInViewport)
	: FEditorViewportSelectability(InOnGetWorld, InOnIsObjectSelectableInViewport)
{
}

bool FSequencerSelectabilityTool::BoxSelect(FBox& InBox, const bool InSelect)
{
	if (!bSelectionLimited
		|| !GCurrentLevelEditingViewportClient
		|| GCurrentLevelEditingViewportClient->IsInGameView())
	{
		return false;
	}

	return BoxSelectWorldActors(InBox, GCurrentLevelEditingViewportClient, InSelect);
}

bool FSequencerSelectabilityTool::FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* const InEditorViewportClient, const bool InSelect)
{
	if (!bSelectionLimited
		|| !InEditorViewportClient
		|| InEditorViewportClient->IsInGameView())
	{
		return false;
	}

	// Need to check for a zero frustum since ComponentIsTouchingSelectionFrustum will return true, selecting everything, when this is the case
	const bool bAreTopBottomMalformed = InFrustum.Planes[0].IsNearlyZero() && InFrustum.Planes[2].IsNearlyZero();
	const bool bAreRightLeftMalformed = InFrustum.Planes[1].IsNearlyZero() && InFrustum.Planes[3].IsNearlyZero();
	if (bAreTopBottomMalformed || bAreRightLeftMalformed)
	{
		return false;
	}

	return FrustumSelectWorldActors(InFrustum, InEditorViewportClient, InSelect);
}

void FSequencerSelectabilityTool::DrawHUD(FEditorViewportClient* const InEditorViewportClient, FViewport* const InViewport, const FSceneView* const InView, FCanvas* InCanvas)
{
	if (!bSelectionLimited)
	{
		return;
	}

	const FSequencerCommands& SequencerCommands = FSequencerCommands::Get();
	const FText HelpText = GetLimitedSelectionText(SequencerCommands.ToggleLimitViewportSelection
		, LOCTEXT("SequencerSelectionLimitHelpText", "Sequencer Selection Limited"));
	DrawEnabledTextNotice(InCanvas, HelpText);
}

#undef LOCTEXT_NAMESPACE
