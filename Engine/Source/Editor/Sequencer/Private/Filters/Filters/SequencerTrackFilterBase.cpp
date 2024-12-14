// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/SequencerTrackFilterBase.h"
#include "Filters/SequencerFilterBar.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "MovieSceneSequence.h"
#include "MVVM/ViewModelPtr.h"
#include "Sequencer.h"

using namespace UE::Sequencer;

FSequencerTrackFilter::FSequencerTrackFilter(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory>&& InCategory)
	: FFilterBase<FSequencerTrackFilterType>(MoveTemp(InCategory))
	, FilterInterface(InOutFilterInterface)
{
}

FText FSequencerTrackFilter::GetToolTipText() const
{
	if (const TSharedPtr<FUICommandInfo> ToggleCommand = GetToggleCommand())
	{
		return BuildTooltipTextForCommand(GetDefaultToolTipText(), ToggleCommand);
	}
	return GetDefaultToolTipText();
}

bool FSequencerTrackFilter::SupportsSequence(UMovieSceneSequence* const InSequence) const
{
	return SupportsLevelSequence(InSequence);
}

void FSequencerTrackFilter::BindCommands()
{
	if (const TSharedPtr<FUICommandInfo> ToggleCommand = GetToggleCommand())
	{
		MapToggleAction(ToggleCommand);
	}
}

ISequencerTrackFilters& FSequencerTrackFilter::GetFilterInterface() const
{
	return FilterInterface;
}

ISequencer& FSequencerTrackFilter::GetSequencer() const
{
	return FilterInterface.GetSequencer();
}

UMovieSceneSequence* FSequencerTrackFilter::GetFocusedMovieSceneSequence() const
{
	return GetSequencer().GetFocusedMovieSceneSequence();
}

UMovieScene* FSequencerTrackFilter::GetFocusedGetMovieScene() const
{
	const UMovieSceneSequence* const FocusedMovieSceneSequence = GetFocusedMovieSceneSequence();
	return IsValid(FocusedMovieSceneSequence) ? FocusedMovieSceneSequence->GetMovieScene() : nullptr;
}

bool FSequencerTrackFilter::SupportsLevelSequence(UMovieSceneSequence* const InSequence)
{
	const UClass* const LevelSequenceClass = ULevelSequence::StaticClass();
	return IsValid(InSequence)
		&& IsValid(LevelSequenceClass)
		&& InSequence->GetClass()->IsChildOf(LevelSequenceClass);
}

bool FSequencerTrackFilter::SupportsUMGSequence(UMovieSceneSequence* const InSequence)
{
	static UClass* const WidgetAnimationClass = FindObject<UClass>(nullptr, TEXT("/Script/UMG.WidgetAnimation"), true);
	return IsValid(InSequence)
		&& IsValid(WidgetAnimationClass)
		&& InSequence->GetClass()->IsChildOf(WidgetAnimationClass);
}

FText FSequencerTrackFilter::BuildTooltipTextForCommand(const FText& InBaseText, const TSharedPtr<FUICommandInfo>& InCommand)
{
	const TSharedRef<const FInputChord> FirstValidChord = InCommand->GetFirstValidChord();
	if (FirstValidChord->IsValidChord())
	{
		return FText::Format(NSLOCTEXT("Sequencer", "TrackFilterTooltipText", "{0} ({1})"), InBaseText, FirstValidChord->GetInputText());
	}
	return InBaseText;
}

bool FSequencerTrackFilter::CanToggleFilter() const
{
	const FString FilterName = GetDisplayName().ToString();
	return FilterInterface.IsFilterActiveByDisplayName(FilterName);
}

void FSequencerTrackFilter::ToggleFilter()
{
	const FString FilterName = GetDisplayName().ToString();
	const bool bNewState = !FilterInterface.IsFilterActiveByDisplayName(FilterName);
	FilterInterface.SetFilterActiveByDisplayName(FilterName, bNewState);
}

void FSequencerTrackFilter::MapToggleAction(const TSharedPtr<FUICommandInfo>& InCommand)
{
	FilterInterface.GetCommandList()->MapAction(
		InCommand,
		FExecuteAction::CreateSP(this, &FSequencerTrackFilter::ToggleFilter),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSequencerTrackFilter::CanToggleFilter));
}
