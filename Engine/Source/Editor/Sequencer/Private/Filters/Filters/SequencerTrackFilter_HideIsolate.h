// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterBase.h"

class FSequencerTrackFilter_HideIsolate : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_HideIsolate(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr);

	void ResetFilter();

	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> GetHiddenTracks() const;
	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> GetIsolatedTracks() const;

	void HideTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks, const bool bInAddToExisting);
	void UnhideTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks);

	void IsolateTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks, const bool bInAddToExisting);
	void UnisolateTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks);

	void IsolateCategoryGroupTracks(const TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>>& InTracks
		, const TSet<FName>& InCategoryNames
		, const bool bInAddToExisting);

	void ShowAllTracks();

	bool HasHiddenTracks() const;
	bool HasIsolatedTracks() const;
	bool HasHiddenOrIsolatedTracks() const;

	bool IsTrackHidden(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InTrack) const;
	bool IsTrackIsolated(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InTrack) const;

	void EmptyHiddenTracks(const bool bInBroadcastChange = true);
	void EmptyIsolatedTracks(const bool bInBroadcastChange = true);

	//~ Begin FSequencerTrackFilter
	virtual void BindCommands() override;
	//~ End FSequencerTrackFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTipText() const override;
	virtual FSlateIcon GetIcon() const override;
	//~ End FFilterBase

	//~ Begin IFilter
	virtual FString GetName() const override;
	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override;
	//~ End IFilter

	bool IsActive() const;

private:
	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> HiddenTracks;
	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> IsolatedTracks;
};
