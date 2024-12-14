// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class FSequencerFilterBar;

class SFilterBarIsolateHideShow : public SCompoundWidget
{
public:
	static FText MakeHiddenTracksSummaryText(FSequencerFilterBar& InFilterBar, const bool bInShowTotalCount);
	static FText MakeIsolatedTracksSummaryText(FSequencerFilterBar& InFilterBar, const bool bInShowTotalCount);
	static FText MakeHideIsolateTracksSummaryText(FSequencerFilterBar& InFilterBar);
	static FText MakeLongDisplaySummaryText(FSequencerFilterBar& InFilterBar);

	SLATE_BEGIN_ARGS(SFilterBarIsolateHideShow)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FSequencerFilterBar>& InFilterBar);

protected:
	TSharedRef<SWidget> ConstructLayeredImage(const FName InBaseImageName, const TAttribute<bool>& InShowBadge);
	
	bool AreFiltersMuted() const;

	FReply HandleHideTracksClick();
	FReply HandleIsolateTracksClick();
	FReply HandleShowAllTracksClick();

	bool HasIsolatedTracks() const;
	bool HasHiddenTracks() const;

	FSlateColor GetShowAllTracksButtonTextColor() const;

	FText GetHideTracksButtonTooltipText() const;
	FText GetIsolateTracksButtonTooltipText() const;
	FText GetShowAllTracksButtonTooltipText() const;

	TWeakPtr<FSequencerFilterBar> WeakFilterBar;
};
