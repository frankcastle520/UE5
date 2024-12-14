// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneTrack.h"
#include "MVVM/ViewModelPtr.h"

class ISequencer;

namespace UE::Sequencer
{
	class IObjectBindingExtension;
	class IOutlinerExtension;
	class ITrackExtension;
}

using FSequencerTrackFilterType = UE::Sequencer::FViewModelPtr;

/** Represents a cache between nodes for a filter operation. */
struct SEQUENCER_API FSequencerFilterData
{
	FSequencerFilterData(const FString& InRawFilterText);

	bool operator==(const FSequencerFilterData& InRhs) const;
	bool operator!=(const FSequencerFilterData& InRhs) const;

	void Reset();

	FString GetRawFilterText() const;

	uint32 GetDisplayNodeCount() const;
	uint32 GetTotalNodeCount() const;

	uint32 GetFilterInCount() const;
	uint32 GetFilterOutCount() const;

	void IncrementTotalNodeCount();

	void FilterInNode(UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension> InNodeWeak);
	void FilterOutNode(UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension> InNodeWeak);

	void FilterInParentChildNodes(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode
		, const bool bInIncludeSelf, const bool bInIncludeParents, const bool bInIncludeChildren = false);

	void FilterInNodeWithAncestors(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode);

	bool ContainsFilterInNodes(const FSequencerFilterData& InOtherData) const;

	bool IsFilteredOut(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode) const;

	UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::ITrackExtension> ResolveTrack(FSequencerTrackFilterType InNode);
	UMovieSceneTrack* ResolveMovieSceneTrackObject(FSequencerTrackFilterType InNode);
	UObject* ResolveTrackBoundObject(ISequencer& InSequencer, FSequencerTrackFilterType InNode);

	TMap<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>, UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::ITrackExtension>> ResolvedTracks;
	TMap<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>, TWeakObjectPtr<UMovieSceneTrack>> ResolvedTrackObjects;
	TMap<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>, TWeakObjectPtr<>> ResolvedObjects;

protected:
	FString RawFilterText;

	uint32 TotalNodeCount = 0;

	/** Nodes to be displayed in the UI */
	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> FilterInNodes;
};
