// Copyright Epic Games, Inc. All Rights Reserved.

#include "Filters/SequencerTextFilterExpressionContext.h"
#include "Filters/SequencerTrackFilterBase.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/ViewModelPtr.h"
#include "Sequencer.h"
#include "MVVM/ViewModels/CategoryModel.h"

using namespace UE::Sequencer;

FSequencerTextFilterExpressionContext::FSequencerTextFilterExpressionContext(ISequencerTrackFilters& InFilterInterface)
	: FilterInterface(InFilterInterface)
{
}

void FSequencerTextFilterExpressionContext::SetFilterItem(FSequencerTrackFilterType InFilterItem, UMovieSceneTrack* const InTrackObject)
{
	FilterItem = InFilterItem;
	WeakTrackObject = InTrackObject;
}

bool FSequencerTextFilterExpressionContext::TestBasicStringExpression(const FTextFilterString& InValue
	, const ETextFilterTextComparisonMode InTextComparisonMode) const
{
	if (const TViewModelPtr<FChannelGroupOutlinerModel> ChannelGroupOutlinerModel = FilterItem.ImplicitCast())
	{
		const FTextFilterString TrackLabel = ChannelGroupOutlinerModel->GetLabel().ToString();
		if (TextFilterUtils::TestBasicStringExpression(TrackLabel, InValue, ETextFilterTextComparisonMode::Partial))
		{
			return true;
		}

		const FTextFilterString ChannelName = ChannelGroupOutlinerModel->GetChannelName();
		if (TextFilterUtils::TestBasicStringExpression(ChannelName, InValue, ETextFilterTextComparisonMode::Partial))
		{
			return true;
		}
	}
	else if (const TViewModelPtr<FCategoryGroupModel> CategoryGroupModel = FilterItem.ImplicitCast())
	{
		const FTextFilterString CategoryName = CategoryGroupModel->GetCategoryName();
		if (TextFilterUtils::TestBasicStringExpression(CategoryName, InValue, ETextFilterTextComparisonMode::Partial))
		{
			return true;
		}
	}
	else if (const TViewModelPtr<FChannelGroupModel> ChannelGroupModel = FilterItem.ImplicitCast())
	{
		const FTextFilterString CategoryName = ChannelGroupModel->GetChannelName();
		if (TextFilterUtils::TestBasicStringExpression(CategoryName, InValue, ETextFilterTextComparisonMode::Partial))
		{
			return true;
		}
	}
	else if (const TViewModelPtr<IOutlinerExtension> OutlinerExtension = FilterItem.ImplicitCast())
	{
		const FTextFilterString TrackLabel = OutlinerExtension->GetLabel().ToString();
		if (TextFilterUtils::TestBasicStringExpression(TrackLabel, InValue, ETextFilterTextComparisonMode::Partial))
		{
			return true;
		}
	}

	return false;
}

bool FSequencerTextFilterExpressionContext::TestComplexExpression(const FName& InKey
	, const FTextFilterString& InValue
	, const ETextFilterComparisonOperation InComparisonOperation
	, const ETextFilterTextComparisonMode InTextComparisonMode) const
{
	if (!FilterItem.IsValid())
	{
		return false;
	}

	const TSet<FName> Keys = GetKeys();
	if (!Keys.IsEmpty() && !Keys.Contains(InKey))
	{
		return false;
	}

	return !InValue.IsEmpty();
}

UMovieSceneSequence* FSequencerTextFilterExpressionContext::GetFocusedMovieSceneSequence() const
{
	return FilterInterface.GetSequencer().GetFocusedMovieSceneSequence();
}

UMovieScene* FSequencerTextFilterExpressionContext::GetFocusedGetMovieScene() const
{
	const UMovieSceneSequence* const FocusedMovieSceneSequence = GetFocusedMovieSceneSequence();
	return IsValid(FocusedMovieSceneSequence) ? FocusedMovieSceneSequence->GetMovieScene() : nullptr;
}

bool FSequencerTextFilterExpressionContext::CompareFStringForExactBool(const FTextFilterString& InValue, const bool bInPassedFilter) const
{
	if (InValue.CompareFString(TEXT("TRUE"), ETextFilterTextComparisonMode::Exact))
	{
		return bInPassedFilter;
	}
	if (InValue.CompareFString(TEXT("FALSE"), ETextFilterTextComparisonMode::Exact))
	{
		return !bInPassedFilter;
	}
	return true;
}

bool FSequencerTextFilterExpressionContext::CompareFStringForExactBool(const FTextFilterString& InValue
	, const ETextFilterComparisonOperation InComparisonOperation
	, const bool bInPassedFilter) const
{
	switch (InComparisonOperation)
	{
	case ETextFilterComparisonOperation::Equal:
		return CompareFStringForExactBool(InValue, bInPassedFilter);
	case ETextFilterComparisonOperation::NotEqual:
		return CompareFStringForExactBool(InValue, !bInPassedFilter);
	default:
		return true;
	}
}
