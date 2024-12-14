// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrackFilter_HideIsolate.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "MVVM/ViewModels/CategoryModel.h"
#include "MVVM/ViewModels/TrackModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "SequencerTrackFilter_HideIsolate"

FSequencerTrackFilter_HideIsolate::FSequencerTrackFilter_HideIsolate(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory)
	: FSequencerTrackFilter(InFilterInterface, MoveTemp(InCategory))
{
	IsActiveEvent = FIsActiveEvent::CreateLambda([this]()
		{
			return !HiddenTracks.IsEmpty() || !IsolatedTracks.IsEmpty();
		});
}

void FSequencerTrackFilter_HideIsolate::BindCommands()
{
	const FSequencerTrackFilterCommands& TrackFilterCommands = FSequencerTrackFilterCommands::Get();

	const TSharedPtr<FUICommandList>& FilterBarBindings = FilterInterface.GetCommandList();

	FilterBarBindings->MapAction(TrackFilterCommands.HideSelectedTracks,
		FUIAction(
			FExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::HideSelectedTracks),
			FCanExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::HasSelectedTracks)
		));

	FilterBarBindings->MapAction(TrackFilterCommands.IsolateSelectedTracks,
		FUIAction(
			FExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::IsolateSelectedTracks),
			FCanExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::HasSelectedTracks)
		));

	FilterBarBindings->MapAction(TrackFilterCommands.ClearHiddenTracks,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerTrackFilter_HideIsolate::EmptyHiddenTracks, true),
			FCanExecuteAction::CreateSP(this, &FSequencerTrackFilter_HideIsolate::HasHiddenTracks)
		));

	FilterBarBindings->MapAction(TrackFilterCommands.ClearIsolatedTracks,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerTrackFilter_HideIsolate::EmptyIsolatedTracks, true),
			FCanExecuteAction::CreateSP(this, &FSequencerTrackFilter_HideIsolate::HasIsolatedTracks)
		));

	FilterBarBindings->MapAction(TrackFilterCommands.ShowAllTracks,
		FUIAction(
			FExecuteAction::CreateSP(this, &FSequencerTrackFilter_HideIsolate::ShowAllTracks),
			FCanExecuteAction::CreateSP(this, &FSequencerTrackFilter_HideIsolate::HasHiddenOrIsolatedTracks)
		));

	FilterBarBindings->MapAction(TrackFilterCommands.ShowLocationCategoryGroups, FExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::ShowOnlyLocationCategoryGroups));
	FilterBarBindings->MapAction(TrackFilterCommands.ShowRotationCategoryGroups, FExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::ShowOnlyRotationCategoryGroups));
	FilterBarBindings->MapAction(TrackFilterCommands.ShowScaleCategoryGroups, FExecuteAction::CreateRaw(&FilterInterface, &ISequencerTrackFilters::ShowOnlyScaleCategoryGroups));
}

void FSequencerTrackFilter_HideIsolate::ResetFilter()
{
	HiddenTracks.Empty();
	IsolatedTracks.Empty();

	BroadcastChangedEvent();
}

TSet<TWeakViewModelPtr<IOutlinerExtension>> FSequencerTrackFilter_HideIsolate::GetHiddenTracks() const
{
	return HiddenTracks;
}

TSet<TWeakViewModelPtr<IOutlinerExtension>> FSequencerTrackFilter_HideIsolate::GetIsolatedTracks() const
{
	return IsolatedTracks;
}

void FSequencerTrackFilter_HideIsolate::HideTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks, const bool bInAddToExisting)
{
	if (!bInAddToExisting)
	{
		HiddenTracks.Empty();
	}

	for (const TWeakViewModelPtr<IOutlinerExtension>& TrackModekWeak : InTracks)
	{
		if (const TViewModelPtr<IOutlinerExtension> TrackModel = TrackModekWeak.Pin())
		{
			HiddenTracks.Add(TrackModel);

			for (const TViewModelPtr<IOutlinerExtension>& ChildNode : TrackModel.AsModel()->GetDescendantsOfType<IOutlinerExtension>())
			{
				HiddenTracks.Add(ChildNode);
			}
		}
	}

	BroadcastChangedEvent();
}

void FSequencerTrackFilter_HideIsolate::UnhideTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks)
{
	for (const TWeakViewModelPtr<IOutlinerExtension>& TrackModekWeak : InTracks)
	{
		if (const TViewModelPtr<IOutlinerExtension> TrackModel = TrackModekWeak.Pin())
		{
			HiddenTracks.Remove(TrackModel);
		}
	}

	BroadcastChangedEvent();
}

void FSequencerTrackFilter_HideIsolate::IsolateTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks, const bool bInAddToExisting)
{
	if (!bInAddToExisting)
	{
		IsolatedTracks.Empty();
	}

	for (const TWeakViewModelPtr<IOutlinerExtension>& TrackModekWeak : InTracks)
	{
		if (const TViewModelPtr<IOutlinerExtension> TrackModel = TrackModekWeak.Pin())
		{
			IsolatedTracks.Add(TrackModel);
		}
	}

	BroadcastChangedEvent();
}

void FSequencerTrackFilter_HideIsolate::UnisolateTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks)
{
	for (const TWeakViewModelPtr<IOutlinerExtension>& TrackModekWeak : InTracks)
	{
		if (const TViewModelPtr<IOutlinerExtension> TrackModel = TrackModekWeak.Pin())
		{
			IsolatedTracks.Remove(TrackModel);
		}
	}

	BroadcastChangedEvent();
}

void FSequencerTrackFilter_HideIsolate::IsolateCategoryGroupTracks(const TSet<TWeakViewModelPtr<IOutlinerExtension>>& InTracks
	, const TSet<FName>& InCategoryNames
	, const bool bInAddToExisting)
{
	TSharedPtr<FSequencerEditorViewModel> SequencerViewModel = GetSequencer().GetViewModel();
	if (!SequencerViewModel.IsValid())
	{
		return;
	}

	if (!bInAddToExisting)
	{
		EmptyIsolatedTracks(false);
	}

	TSet<TWeakViewModelPtr<IOutlinerExtension>> TracksToIsolate;
	TSet<TViewModelPtr<IOutlinerExtension>> TracksToExpand;

	auto IsolateChildCategoryGroups = [this, &InCategoryNames, &TracksToIsolate, &TracksToExpand](const TViewModelPtr<IOutlinerExtension>& InTrack)
	{
		const TParentFirstChildIterator<FCategoryGroupModel> ChildTracks = InTrack.AsModel()->GetDescendantsOfType<FCategoryGroupModel>(true);
		for (const TViewModelPtr<FCategoryGroupModel>& ChildCategoryGroup : ChildTracks)
		{
			if (InCategoryNames.Contains(ChildCategoryGroup->GetCategoryName()))
			{
				TracksToIsolate.Add(ChildCategoryGroup);

				const TParentModelIterator<IOutlinerExtension> Ancestors = ChildCategoryGroup->GetAncestorsOfType<IOutlinerExtension>();
				TracksToExpand.Append(Ancestors.ToArray());
				TracksToExpand.Add(ChildCategoryGroup);
			}
		}
	};

	for (const TWeakViewModelPtr<IOutlinerExtension>& WeakTrack : InTracks)
	{
		const TViewModelPtr<IOutlinerExtension> Track = WeakTrack.Pin();
		if (!Track.IsValid())
		{
			continue;
		}

		const TViewModelPtr<FCategoryGroupModel> CategoryGroupModel = Track.AsModel()->FindAncestorOfType<FCategoryGroupModel>(true);
		if (CategoryGroupModel.IsValid())
		{
			const TViewModelPtr<FTrackModel> ParentTrack = Track.AsModel()->FindAncestorOfType<FTrackModel>();
			if (ParentTrack.IsValid())
			{
				IsolateChildCategoryGroups(ParentTrack);
			}
		}
		else
		{
			IsolateChildCategoryGroups(Track);
		}
	}

	IsolateTracks(TracksToIsolate, true);

	for (const TViewModelPtr<IOutlinerExtension>& Track : TracksToExpand)
	{
		Track->SetExpansion(true);
	}
}

void FSequencerTrackFilter_HideIsolate::ShowAllTracks()
{
	HiddenTracks.Empty();
	IsolatedTracks.Empty();

	BroadcastChangedEvent();
}

bool FSequencerTrackFilter_HideIsolate::HasHiddenTracks() const
{
	return !HiddenTracks.IsEmpty();
}

bool FSequencerTrackFilter_HideIsolate::HasIsolatedTracks() const
{
	return !IsolatedTracks.IsEmpty();
}

bool FSequencerTrackFilter_HideIsolate::HasHiddenOrIsolatedTracks() const
{
	return HasHiddenTracks() || HasIsolatedTracks();
}

bool FSequencerTrackFilter_HideIsolate::IsTrackHidden(const TViewModelPtr<IOutlinerExtension>& InTrack) const
{
	if (HiddenTracks.Contains(InTrack))
	{
		return true;
	}

	for (const TViewModelPtr<IOutlinerExtension>& ParentNode : InTrack.AsModel()->GetAncestorsOfType<IOutlinerExtension>())
	{
		if (HiddenTracks.Contains(ParentNode))
		{
			return true;
		}
	}

	return false;
}

bool FSequencerTrackFilter_HideIsolate::IsTrackIsolated(const TViewModelPtr<IOutlinerExtension>& InTrack) const
{
	if (IsolatedTracks.Contains(InTrack))
	{
		return true;
	}

	for (const TViewModelPtr<IOutlinerExtension>& ParentNode : InTrack.AsModel()->GetAncestorsOfType<IOutlinerExtension>())
	{
		if (IsolatedTracks.Contains(ParentNode))
		{
			return true;
		}
	}

	return false;
}

void FSequencerTrackFilter_HideIsolate::EmptyHiddenTracks(const bool bInBroadcastChange)
{
	HiddenTracks.Empty();

	if (bInBroadcastChange)
	{
		BroadcastChangedEvent();
	}
}

void FSequencerTrackFilter_HideIsolate::EmptyIsolatedTracks(const bool bInBroadcastChange)
{
	IsolatedTracks.Empty();

	if (bInBroadcastChange)
	{
		BroadcastChangedEvent();
	}
}

FText FSequencerTrackFilter_HideIsolate::GetDisplayName() const
{
	return LOCTEXT("SequencerTrackFilter_HideIsolate", "Hidden and Isolated");
}

FText FSequencerTrackFilter_HideIsolate::GetToolTipText() const
{
	return LOCTEXT("SequencerTrackFilter_HideIsolateToolTip", "Show only Hidden and Isolated tracks");
}

FSlateIcon FSequencerTrackFilter_HideIsolate::GetIcon() const
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ExternalImagePicker.BlankImage"));
}

FString FSequencerTrackFilter_HideIsolate::GetName() const
{
	return TEXT("HideIsolate");
}

bool FSequencerTrackFilter_HideIsolate::PassesFilter(FSequencerTrackFilterType InItem) const
{
	const TViewModelPtr<IOutlinerExtension> Track = InItem.ImplicitCast();
	if (!Track)
	{
		return false;
	}

	const bool bHasHiddenTracks = !HiddenTracks.IsEmpty();
	const bool bContainsHiddenTrack = IsTrackHidden(Track);
	const bool bHasIsolatedTracks = !IsolatedTracks.IsEmpty();
	const bool bContainsIsolatedTrack = IsTrackIsolated(Track);

	const bool bIsHidden = bHasHiddenTracks && bContainsHiddenTrack;
	const bool bIsIsolated = bHasIsolatedTracks && bContainsIsolatedTrack;

	// Can hide isolated tracks, but CANNOT isolate hidden tracks

	if (bIsHidden)
	{
		return false; // Filter out
	}

	if (bIsIsolated)
	{
		return !bIsHidden; // Filter in if not hidden
	}

	return !bHasIsolatedTracks; // Filter out if there are isolated tracks and this isn't one
}

bool FSequencerTrackFilter_HideIsolate::IsActive() const
{
	return HasHiddenOrIsolatedTracks();
}

#undef LOCTEXT_NAMESPACE
