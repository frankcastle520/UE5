// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/SequencerFilterData.h"
#include "ISequencer.h"
#include "MVVM/Extensions/IObjectBindingExtension.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/Extensions/ITrackExtension.h"
#include "MVVM/ViewModels/ViewModelIterators.h"

using namespace UE::Sequencer;

FSequencerFilterData::FSequencerFilterData(const FString& InRawFilterText)
	: RawFilterText(InRawFilterText)
{
}

bool FSequencerFilterData::operator==(const FSequencerFilterData& InRhs) const
{
	return ContainsFilterInNodes(InRhs) && TotalNodeCount == InRhs.GetTotalNodeCount();
}

bool FSequencerFilterData::operator!=(const FSequencerFilterData& InRhs) const
{
	return !(*this == InRhs);
}

void FSequencerFilterData::Reset()
{
	FilterInNodes.Reset();

	TotalNodeCount = 0;
}

FString FSequencerFilterData::GetRawFilterText() const
{
	return RawFilterText;
}

uint32 FSequencerFilterData::GetDisplayNodeCount() const
{
	return FilterInNodes.Num();
}

uint32 FSequencerFilterData::GetTotalNodeCount() const
{
	return TotalNodeCount;
}

uint32 FSequencerFilterData::GetFilterInCount() const
{
	return FilterInNodes.Num();
}

uint32 FSequencerFilterData::GetFilterOutCount() const
{
	return GetTotalNodeCount() - GetFilterInCount();
}

void FSequencerFilterData::IncrementTotalNodeCount()
{
	++TotalNodeCount;
}

void FSequencerFilterData::FilterInNode(TWeakViewModelPtr<IOutlinerExtension> InNodeWeak)
{
	FilterInNodes.Add(InNodeWeak);

	if (const TViewModelPtr<IOutlinerExtension> Node = InNodeWeak.Pin())
	{
		Node->SetFilteredOut(false);
	}
}

void FSequencerFilterData::FilterOutNode(TWeakViewModelPtr<IOutlinerExtension> InNodeWeak)
{
	const FSetElementId ElementId = FilterInNodes.FindId(InNodeWeak);
	if (ElementId.IsValidId())
	{
		FilterInNodes.Remove(ElementId);
	}

	if (const TViewModelPtr<IOutlinerExtension> Node = InNodeWeak.Pin())
	{
		Node->SetFilteredOut(true);
	}
}

void FSequencerFilterData::FilterInParentChildNodes(const TViewModelPtr<IOutlinerExtension>& InNode
	, const bool bInIncludeSelf, const bool bInIncludeParents, const bool bInIncludeChildren)
{
	if (!InNode.IsValid())
	{
		return;
	}

	if (bInIncludeParents)
	{
		for (TViewModelPtr<IOutlinerExtension> ParentNode : InNode.AsModel()->GetAncestorsOfType<IOutlinerExtension>())
		{
			FilterInNode(ParentNode);
		}
	}

	if (bInIncludeSelf)
	{
		FilterInNode(InNode);
	}

	if (bInIncludeChildren)
	{
		for (TViewModelPtr<IOutlinerExtension> ChildNode : InNode.AsModel()->GetDescendantsOfType<IOutlinerExtension>())
		{
			FilterInNode(ChildNode);
		}
	}
}

void FSequencerFilterData::FilterInNodeWithAncestors(const TViewModelPtr<IOutlinerExtension>& InNode)
{
	FilterInParentChildNodes(InNode, true, true, false);
}

bool FSequencerFilterData::ContainsFilterInNodes(const FSequencerFilterData& InOtherData) const
{
	return FilterInNodes.Includes(InOtherData.FilterInNodes);
}

bool FSequencerFilterData::IsFilteredOut(const TViewModelPtr<IOutlinerExtension>& InNode) const
{
	return !FilterInNodes.Contains(InNode);
}

TWeakViewModelPtr<ITrackExtension> FSequencerFilterData::ResolveTrack(FSequencerTrackFilterType InNode)
{
	if (!InNode.IsValid())
	{
		return nullptr;
	}

	const TWeakViewModelPtr<IOutlinerExtension> WeakOutlinerNode = InNode.ImplicitCast();

	// Use cache version if it exists, otherwise resolve below
	if (ResolvedTracks.Contains(WeakOutlinerNode))
	{
		if (const TViewModelPtr<ITrackExtension> ResolvedTrack = ResolvedTracks[WeakOutlinerNode].Pin())
		{
			return ResolvedTrack;
		}

		ResolvedTracks.Remove(WeakOutlinerNode);
	}

	// Resolve and cache
	if (const TViewModelPtr<ITrackExtension> AncestorTrack = InNode->FindAncestorOfType<ITrackExtension>(true))
	{
		ResolvedTracks.Add(WeakOutlinerNode, AncestorTrack);
		return AncestorTrack;
	}

	return nullptr;
}

UMovieSceneTrack* FSequencerFilterData::ResolveMovieSceneTrackObject(FSequencerTrackFilterType InNode)
{
	if (!InNode.IsValid())
	{
		return nullptr;
	}

	const TWeakViewModelPtr<IOutlinerExtension> WeakOutlinerNode = InNode.ImplicitCast();

	// Use cache version if it exists, otherwise resolve below
	if (ResolvedTrackObjects.Contains(WeakOutlinerNode))
	{
		if (ResolvedTrackObjects[WeakOutlinerNode].IsValid())
		{
			return ResolvedTrackObjects[WeakOutlinerNode].Get();
		}

		ResolvedTrackObjects.Remove(WeakOutlinerNode);
	}

	if (const TViewModelPtr<ITrackExtension> AncestorTrackModel = InNode->FindAncestorOfType<ITrackExtension>(true))
	{
		UMovieSceneTrack* const TrackObject = AncestorTrackModel->GetTrack();
		if (IsValid(TrackObject))
		{
			ResolvedTrackObjects.Add(WeakOutlinerNode, TrackObject);
			return TrackObject;
		}
	}

	return nullptr;
}

UObject* FSequencerFilterData::ResolveTrackBoundObject(ISequencer& InSequencer, FSequencerTrackFilterType InNode)
{
	if (!InNode.IsValid())
	{
		return nullptr;
	}

	const TWeakViewModelPtr<IOutlinerExtension> WeakOutlinerNode = InNode.ImplicitCast();

	// Use cache version if it exists, otherwise resolve below
	if (ResolvedObjects.Contains(WeakOutlinerNode))
	{
		if (ResolvedObjects[WeakOutlinerNode].IsValid())
		{
			return ResolvedObjects[WeakOutlinerNode].Get();
		}

		ResolvedObjects.Remove(WeakOutlinerNode);
	}

	if (const TViewModelPtr<IObjectBindingExtension> ObjectBindingModel = InNode->FindAncestorOfType<IObjectBindingExtension>(true))
	{
		UObject* const BoundObject = InSequencer.FindSpawnedObjectOrTemplate(ObjectBindingModel->GetObjectGuid());
		if (IsValid(BoundObject))
		{
			ResolvedObjects.Add(WeakOutlinerNode, BoundObject);
			return BoundObject;
		}
	}

	return nullptr;
}
