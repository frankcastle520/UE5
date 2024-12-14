// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterBase.h"
#include "SSequencerFilterBar.h"

class FSequencerTrackFilterContextMenu;
class SSequencerFilterCheckBox;

class SSequencerFilter : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSequencerFilterRequest, const TSharedRef<SSequencerFilter>& /*InFilter*/);

	SLATE_BEGIN_ARGS(SSequencerFilter) {}
		/** Determines how each individual filter pill looks like */
		SLATE_ARGUMENT(EFilterPillStyle, FilterPillStyle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs
		, const TSharedRef<FSequencerFilterBar>& InFilterBar
		, const TSharedRef<FSequencerTrackFilter>& InFilter);

	const TSharedPtr<FSequencerTrackFilter> GetFilter() const;

protected:
	TSharedRef<SWidget> ConstructBasicFilterWidget();
	TSharedRef<SWidget> ConstructDefaultFilterWidget();

	bool IsActive() const;

	void OnFilterToggled(const ECheckBoxState NewState);

	FReply OnFilterCtrlClick();
	FReply OnFilterAltClick();
	FReply OnFilterMiddleButtonClick();
	FReply OnFilterDoubleClick();

	TSharedRef<SWidget> GetRightClickMenuContent();

	ECheckBoxState IsChecked() const;

	FSlateColor GetFilterImageColorAndOpacity() const;

	EVisibility GetFilterOverlayVisibility() const;

	FMargin GetFilterNamePadding() const;

	FText GetFilterDisplayName() const;

	bool IsButtonEnabled() const;

	void ActivateAllButThis(const bool bInActive);

	TWeakPtr<FSequencerFilterBar> WeakFilterBar;

	TWeakPtr<FSequencerTrackFilter> WeakFilter;

	TSharedPtr<SSequencerFilterCheckBox> ToggleButtonPtr;

	TSharedPtr<FSequencerTrackFilterContextMenu> ContextMenu;
};
