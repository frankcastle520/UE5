// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTextFilterExpressionContext.h"

class IKeyArea;

class FSequencerTextFilterExpression_Time : public FSequencerTextFilterExpressionContext
{
public:
	static bool CompareTime(ISequencer& InSequencer
		, const FTextFilterString& InValue
		, const TArray<TSharedRef<IKeyArea>>& InKeyAreas
		, const ETextFilterComparisonOperation InComparisonOperation);

	FSequencerTextFilterExpression_Time(ISequencerTrackFilters& InFilterInterface);

	//~ Begin FSequencerTextFilterExpressionContext
	virtual TSet<FName> GetKeys() const override;
	virtual ESequencerTextFilterValueType GetValueType() const override;
	virtual FText GetDescription() const override;
	virtual TArray<FSequencerTextFilterKeyword> GetValueKeywords() const override;
	//~ End FSequencerTextFilterExpressionContext

	//~ Begin ITextFilterExpressionContext
	virtual bool TestComplexExpression(const FName& InKey
		, const FTextFilterString& InValue
		, const ETextFilterComparisonOperation InComparisonOperation
		, const ETextFilterTextComparisonMode InTextComparisonMode) const override;
	//~ End ITextFilterExpressionContext
};
