// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFilterBarIsolateHideShow.h"
#include "Filters/SequencerFilterBar.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SFilterBarIsolateHideShow"

void SFilterBarIsolateHideShow::Construct(const FArguments& InArgs, const TSharedRef<FSequencerFilterBar>& InFilterBar)
{
	WeakFilterBar = InFilterBar;

	constexpr float ButtonContentPadding = 2.f;
	constexpr float ButtonSpacing = 1.f;

	ChildSlot
	[
		SNew(SHorizontalBox)

		// Isolate Selected Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, ButtonSpacing, 0.f)
		[
			SNew(SButton)
			.ContentPadding(ButtonContentPadding)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ToolTipText(this, &SFilterBarIsolateHideShow::GetIsolateTracksButtonTooltipText)
			.IsEnabled(this, &SFilterBarIsolateHideShow::AreFiltersMuted)
			.OnClicked(this, &SFilterBarIsolateHideShow::HandleIsolateTracksClick)
			[
				ConstructLayeredImage(TEXT("Sequencer.TrackIsolate")
					, TAttribute<bool>::CreateSP(this, &SFilterBarIsolateHideShow::HasIsolatedTracks))
			]
		]

		// Hide Selected Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, ButtonSpacing, 0.f)
		[
			SNew(SButton)
			.ContentPadding(ButtonContentPadding)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ToolTipText(this, &SFilterBarIsolateHideShow::GetHideTracksButtonTooltipText)
			.IsEnabled(this, &SFilterBarIsolateHideShow::AreFiltersMuted)
			.OnClicked(this, &SFilterBarIsolateHideShow::HandleHideTracksClick)
			[
				ConstructLayeredImage(TEXT("Sequencer.TrackHide")
					, TAttribute<bool>::CreateSP(this, &SFilterBarIsolateHideShow::HasHiddenTracks))
			]
		]

		// Show All Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ContentPadding(ButtonContentPadding)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ToolTipText(this, &SFilterBarIsolateHideShow::GetShowAllTracksButtonTooltipText)
			.IsEnabled(this, &SFilterBarIsolateHideShow::AreFiltersMuted)
			.OnClicked(this, &SFilterBarIsolateHideShow::HandleShowAllTracksClick)
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(16.f))
				.ColorAndOpacity(this, &SFilterBarIsolateHideShow::GetShowAllTracksButtonTextColor)
				.Image(FAppStyle::Get().GetBrush(TEXT("Sequencer.TrackShow")))
			]
		]
	];
}

TSharedRef<SWidget> SFilterBarIsolateHideShow::ConstructLayeredImage(const FName InBaseImageName, const TAttribute<bool>& InShowBadge)
{
	const TSharedRef<SLayeredImage> LayeredImage = SNew(SLayeredImage)
		.DesiredSizeOverride(FVector2D(16.f))
		.ColorAndOpacity(FStyleColors::Foreground)
		.Image(FAppStyle::Get().GetBrush(InBaseImageName));

	LayeredImage->AddLayer(TAttribute<const FSlateBrush*>::CreateLambda([InShowBadge]() -> const FSlateBrush*
		{
			return InShowBadge.Get(false)
				? FAppStyle::Get().GetBrush(TEXT("Icons.BadgeModified"))
				: nullptr;
		}));

	return LayeredImage;
}

bool SFilterBarIsolateHideShow::AreFiltersMuted() const
{
	return WeakFilterBar.IsValid() ? !WeakFilterBar.Pin()->AreFiltersMuted() : true;
}

FReply SFilterBarIsolateHideShow::HandleHideTracksClick()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FReply::Handled();
	}

	const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
	const bool bControlDown = ModifierKeys.AreModifersDown(EModifierKey::Control);
	if (bControlDown)
	{
		FilterBar->EmptyHiddenTracks();
	}
	else
	{
		FilterBar->HideSelectedTracks();
	}

	return FReply::Handled();
}

FReply SFilterBarIsolateHideShow::HandleIsolateTracksClick()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FReply::Handled();
	}

	const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
	const bool bControlDown = ModifierKeys.AreModifersDown(EModifierKey::Control);
	if (bControlDown)
	{
		FilterBar->EmptyIsolatedTracks();
	}
	else
	{
		FilterBar->IsolateSelectedTracks();
	}

	return FReply::Handled();
}

FReply SFilterBarIsolateHideShow::HandleShowAllTracksClick()
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FReply::Handled();
	}

	FilterBar->ShowAllTracks();

	return FReply::Handled();
}

bool SFilterBarIsolateHideShow::HasIsolatedTracks() const
{
	return WeakFilterBar.IsValid() && WeakFilterBar.Pin()->GetIsolatedTracks().Num() > 0;
}

bool SFilterBarIsolateHideShow::HasHiddenTracks() const
{
	return WeakFilterBar.IsValid() && WeakFilterBar.Pin()->GetHiddenTracks().Num() > 0;
}

FSlateColor SFilterBarIsolateHideShow::GetShowAllTracksButtonTextColor() const
{
	if (const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin())
	{
		return (FilterBar->GetHiddenTracks().Num() == 0 && FilterBar->GetIsolatedTracks().Num() == 0)
			? FStyleColors::Foreground : FStyleColors::Warning;
	}
	return FStyleColors::Foreground;
}

FText SFilterBarIsolateHideShow::GetHideTracksButtonTooltipText() const
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FText::GetEmpty();
	}

	FText TooltipText = LOCTEXT("HideTracksButtonToolTip", "Hide selected tracks");

	const TSharedRef<const FInputChord> Chord = FSequencerTrackFilterCommands::Get().HideSelectedTracks->GetFirstValidChord();
	if (Chord->IsValidChord())
	{
		TooltipText = FText::Format(LOCTEXT("HideTracksButtonToolTipExtended", "{0} ({1})")
			, TooltipText, Chord->GetInputText());
	}

	const FText SummaryWithTotal = MakeHiddenTracksSummaryText(*FilterBar, true);
	TooltipText = FText::Format(LOCTEXT("HideTracksButtonToolTipExtendedWithTotal", "{0}\n\n"
		"Use the Control modifier to reset the hidden track list.\n\n{1}")
		, TooltipText, SummaryWithTotal);

	return TooltipText;
}

FText SFilterBarIsolateHideShow::GetIsolateTracksButtonTooltipText() const
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FText::GetEmpty();
	}

	FText TooltipText = LOCTEXT("IsolateTracksButtonToolTip", "Isolate selected tracks");

	const TSharedRef<const FInputChord> Chord = FSequencerTrackFilterCommands::Get().IsolateSelectedTracks->GetFirstValidChord();
	if (Chord->IsValidChord())
	{
		TooltipText = FText::Format(LOCTEXT("IsolateTracksButtonToolTipExtended", "{0} ({1})")
			, TooltipText, Chord->GetInputText());
	}

	const FText SummaryWithTotal = MakeIsolatedTracksSummaryText(*FilterBar, true);
	TooltipText = FText::Format(LOCTEXT("IsolateTracksButtonToolTipExtendedWithTotal", "{0}\n\n"
		"Use the Shift modifier to additively isolate.\n"
		"Use the Control modifier to reset the isolated track list.\n\n{1}")
		, TooltipText, SummaryWithTotal);

	return TooltipText;
}

FText SFilterBarIsolateHideShow::GetShowAllTracksButtonTooltipText() const
{
	const TSharedPtr<FSequencerFilterBar> FilterBar = WeakFilterBar.Pin();
	if (!FilterBar.IsValid())
	{
		return FText::GetEmpty();
	}

	FText TooltipText = LOCTEXT("ShowAllTracksButtonToolTip", "Show all tracks");

	const TSharedRef<const FInputChord> Chord = FSequencerTrackFilterCommands::Get().ShowAllTracks->GetFirstValidChord();
	if (Chord->IsValidChord())
	{
		TooltipText = FText::Format(LOCTEXT("ShowAllTracksButtonToolTipExtended", "{0} ({1})")
			, TooltipText, Chord->GetInputText());
	}

	const FText SummaryText = MakeLongDisplaySummaryText(*FilterBar);
	TooltipText = FText::Format(LOCTEXT("ShowAllTracksButtonToolTipExtendedWithSummary", "{0}\n\n{1}")
		, TooltipText, SummaryText);

	return TooltipText;
}

FText SFilterBarIsolateHideShow::MakeHiddenTracksSummaryText(FSequencerFilterBar& InFilterBar, const bool bInShowTotalCount)
{
	FText Summary = FText::Format(LOCTEXT("HiddenTracksSummary", "{0} hidden tracks")
		, InFilterBar.GetHiddenTracks().Num());

	if (bInShowTotalCount)
	{
		Summary = FText::Format(LOCTEXT("HiddenTracksSummaryWithTotal", "{0} of {1} total tracks")
			, Summary, InFilterBar.GetFilterData().GetTotalNodeCount());
	}

	return Summary;
}

FText SFilterBarIsolateHideShow::MakeIsolatedTracksSummaryText(FSequencerFilterBar& InFilterBar, const bool bInShowTotalCount)
{
	FText Summary = FText::Format(LOCTEXT("IsolatedTracksSummary", "{0} isolated tracks")
		, InFilterBar.GetIsolatedTracks().Num());

	if (bInShowTotalCount)
	{
		Summary = FText::Format(LOCTEXT("IsolatedTracksSummaryWithTotal", "{0} of {1} total tracks")
			, Summary, InFilterBar.GetFilterData().GetTotalNodeCount());
	}

	return Summary;
}

FText SFilterBarIsolateHideShow::MakeHideIsolateTracksSummaryText(FSequencerFilterBar& InFilterBar)
{
	const FText HiddenTracksSummary = MakeHiddenTracksSummaryText(InFilterBar, false);
	const FText IsolatedTracksSummary = MakeIsolatedTracksSummaryText(InFilterBar, false);
	return FText::Format(LOCTEXT("HideIsolateSummary", "{0}, {1}")
		, HiddenTracksSummary, IsolatedTracksSummary);
}

FText SFilterBarIsolateHideShow::MakeLongDisplaySummaryText(FSequencerFilterBar& InFilterBar)
{
	const FSequencerFilterData& FilterData = InFilterBar.GetFilterData();
	const int32 FilteredCount = FilterData.GetDisplayNodeCount();
	const int32 TotalCount = FilterData.GetTotalNodeCount();
	const FText HideIsolateSummary = MakeHideIsolateTracksSummaryText(InFilterBar);

	return FText::Format(LOCTEXT("LongDisplaySummary", "Showing {0} of {1} total tracks\n{2}"),
		FilteredCount, TotalCount, HideIsolateSummary);
}

#undef LOCTEXT_NAMESPACE
