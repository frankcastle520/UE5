// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrackFilter_Keyed.h"
#include "Filters/SequencerFilterBar.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "Framework/Commands/Commands.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/ViewModelPtr.h"
#include "MVVM/ViewModels/CategoryModel.h"
#include "Sequencer.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "SequencerTrackFilter_Keyed"

FSequencerTrackFilter_Keyed::FSequencerTrackFilter_Keyed(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory)
	: FSequencerTrackFilter(InFilterInterface, MoveTemp(InCategory))
{
}

bool FSequencerTrackFilter_Keyed::ShouldUpdateOnTrackValueChanged() const
{
	return true;
}

FText FSequencerTrackFilter_Keyed::GetDefaultToolTipText() const
{
	return LOCTEXT("SequencerTrackFilter_KeyedTip", "Show only Keyed tracks"); 
}

TSharedPtr<FUICommandInfo> FSequencerTrackFilter_Keyed::GetToggleCommand() const
{
	return FSequencerTrackFilterCommands::Get().ToggleFilter_Keyed;
}

bool FSequencerTrackFilter_Keyed::SupportsSequence(UMovieSceneSequence* const InSequence) const
{
	return FSequencerTrackFilter::SupportsSequence(InSequence) || SupportsUMGSequence(InSequence);
}

bool FSequencerTrackFilter_Keyed::PassesFilter(FSequencerTrackFilterType InItem) const
{
	if (const TViewModelPtr<FCategoryGroupModel> CategoryGroupModel = InItem.ImplicitCast())
	{
		for (const TWeakViewModelPtr<FCategoryModel>& WeakCategory : CategoryGroupModel->GetCategories())
		{
			if (const TViewModelPtr<FCategoryModel> Category = WeakCategory.Pin())
			{
				return Category->IsAnimated();
			}
		}
	}
	else if (const TViewModelPtr<FChannelGroupOutlinerModel> ChannelGroupOutlinerModel = InItem.ImplicitCast())
	{
		return ChannelGroupOutlinerModel->IsAnimated();
	}

	const TWeakViewModelPtr<ITrackExtension> WeakTrack = FilterInterface.GetFilterData().ResolveTrack(InItem);
	return DoesTrackExtensionHaveKeys(WeakTrack);
}

FText FSequencerTrackFilter_Keyed::GetDisplayName() const
{
	return LOCTEXT("SequenceTrackFilter_Keyed", "Keyed");
}

FSlateIcon FSequencerTrackFilter_Keyed::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.IconKeyUser"));
}

FString FSequencerTrackFilter_Keyed::GetName() const
{
	return TEXT("Keyed");
}

bool FSequencerTrackFilter_Keyed::DoesTrackExtensionHaveKeys(const TWeakViewModelPtr<ITrackExtension>& InTrack)
{
	const TViewModelPtr<ITrackExtension> Track = InTrack.Pin();
	if (!Track.IsValid())
	{
		return false;
	}

	UMovieSceneTrack* const TrackObject = Track->GetTrack();
	if (!IsValid(TrackObject))
	{
		return false;
	}

	const int32 RowIndex = Track->GetRowIndex();

	for (const UMovieSceneSection* const Section : TrackObject->GetAllSections())
	{
		if (Section->GetRowIndex() == RowIndex)
		{
			for (const FMovieSceneChannelEntry& ChannelEntry : Section->GetChannelProxy().GetAllEntries())
			{
				const TConstArrayView<FMovieSceneChannel*> Channels = ChannelEntry.GetChannels();
				for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
				{
					const FMovieSceneChannel* const Channel = Channels[ChannelIndex];
					if (Channel && Channel->GetNumKeys() > 0)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
