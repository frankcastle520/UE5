// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTextFilterExpression_Name.h"
#include "MovieSceneNameableTrack.h"
#include "MVVM/ViewModelPtr.h"
#include "MVVM/Extensions/IOutlinerExtension.h"

using namespace UE::Sequencer;

#define LOCTEXT_NAMESPACE "SequencerTextFilterExpression_Name"

FSequencerTextFilterExpression_Name::FSequencerTextFilterExpression_Name(ISequencerTrackFilters& InFilterInterface)
	: FSequencerTextFilterExpressionContext(InFilterInterface)
{
}

TSet<FName> FSequencerTextFilterExpression_Name::GetKeys() const
{
	return { TEXT("Name") };
}

ESequencerTextFilterValueType FSequencerTextFilterExpression_Name::GetValueType() const
{
	return ESequencerTextFilterValueType::String;
}

FText FSequencerTextFilterExpression_Name::GetDescription() const
{
	return LOCTEXT("ExpressionDescription_Name", "Filter by track name");
}

bool FSequencerTextFilterExpression_Name::TestComplexExpression(const FName& InKey
	, const FTextFilterString& InValue
	, const ETextFilterComparisonOperation InComparisonOperation
	, const ETextFilterTextComparisonMode InTextComparisonMode) const
{
	if (!FSequencerTextFilterExpressionContext::TestComplexExpression(InKey, InValue, InComparisonOperation, InTextComparisonMode))
	{
		return true;
	}

	if (WeakTrackObject.IsValid())
	{
		if (TextFilterUtils::TestComplexExpression(WeakTrackObject->GetName(), InValue, InComparisonOperation, InTextComparisonMode))
		{
			return true;
		}

		UMovieSceneNameableTrack* const NameableTrack = Cast<UMovieSceneNameableTrack>(WeakTrackObject);
		if (IsValid(NameableTrack))
		{
			const FString DisplayName = NameableTrack->GetDisplayName().ToString();
			if (TextFilterUtils::TestComplexExpression(DisplayName, InValue, InComparisonOperation, InTextComparisonMode))
			{
				return true;
			}
		}
	}

	if (const TViewModelPtr<IOutlinerExtension> OutlinerExtension = FilterItem.ImplicitCast())
	{
		if (TextFilterUtils::TestComplexExpression(OutlinerExtension->GetLabel().ToString(), InValue, InComparisonOperation, InTextComparisonMode))
		{
			return true;
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
