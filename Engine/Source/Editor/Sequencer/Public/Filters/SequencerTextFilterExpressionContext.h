// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterBase.h"
#include "Misc/TextFilterExpressionEvaluator.h"

class UMovieScene;
class UMovieSceneSequence;
class UMovieSceneTrack;

enum class SEQUENCER_API ESequencerTextFilterValueType : uint8
{
	String,
	Boolean,
	Integer
};

struct SEQUENCER_API FSequencerTextFilterKeyword
{
	FString Keyword;
	FText Description;
};

/** Text expression context to test the given asset data against the current text filter */
class FSequencerTextFilterExpressionContext : public ITextFilterExpressionContext
{ 
public:
	SEQUENCER_API FSequencerTextFilterExpressionContext(ISequencerTrackFilters& InFilterInterface);

	void SetFilterItem(FSequencerTrackFilterType InFilterItem, UMovieSceneTrack* const InTrackObject);

	//~ Begin FSequencerTextFilterExpressionContext

	SEQUENCER_API virtual TSet<FName> GetKeys() const = 0;

	SEQUENCER_API virtual ESequencerTextFilterValueType GetValueType() const = 0;
	SEQUENCER_API virtual TArray<FSequencerTextFilterKeyword> GetValueKeywords() const { return {}; }

	SEQUENCER_API virtual FText GetDescription() const = 0;
	SEQUENCER_API virtual FText GetCategory() const { return FText::GetEmpty(); }

	//~ End FSequencerTextFilterExpressionContext

	//~ Begin ITextFilterExpressionContext

	SEQUENCER_API virtual bool TestBasicStringExpression(const FTextFilterString& InValue
		, const ETextFilterTextComparisonMode InTextComparisonMode) const override;

	SEQUENCER_API virtual bool TestComplexExpression(const FName& InKey
		, const FTextFilterString& InValue
		, const ETextFilterComparisonOperation InComparisonOperation
		, const ETextFilterTextComparisonMode InTextComparisonMode) const override;

	//~ End ITextFilterExpressionContext

protected:
	SEQUENCER_API UMovieSceneSequence* GetFocusedMovieSceneSequence() const;
	SEQUENCER_API UMovieScene* GetFocusedGetMovieScene() const;

	SEQUENCER_API bool CompareFStringForExactBool(const FTextFilterString& InValue, const bool bInPassedFilter) const;

	SEQUENCER_API bool CompareFStringForExactBool(const FTextFilterString& InValue
		, const ETextFilterComparisonOperation InComparisonOperation
		, const bool bInPassedFilter) const;

	ISequencerTrackFilters& FilterInterface;

	FSequencerTrackFilterType FilterItem;

	TWeakObjectPtr<UMovieSceneTrack> WeakTrackObject;
};
