// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTextFilterExpressionContext.h"
#include "Filters/SequencerTrackFilterBase.h"

class FSequencerTrackFilter_Text : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_Text(ISequencerTrackFilters& InFilterInterface);

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTipText() const override;
	//~ End FFilterBase

	//~ Begin IFilter
	virtual FString GetName() const override;
	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override;
	//~ End IFilter

	bool IsActive() const;

	FText GetRawFilterText() const;
	void SetRawFilterText(const FText& InFilterText);

	FText GetFilterErrorText() const;

	const TArray<TSharedRef<FSequencerTextFilterExpressionContext>>& GetTextFilterExpressionContexts() const;

	bool DoesTextFilterStringContainExpressionPair(const FSequencerTextFilterExpressionContext& InExpression) const;

protected:
	static bool IsTokenKey(const FExpressionToken& InToken, const TSet<FName>& InKeys);
	static bool IsTokenOperator(const FExpressionToken& InToken, const ESequencerTextFilterValueType InValueType);
	static bool IsTokenValueValid(const FExpressionToken& InToken, const ESequencerTextFilterValueType InValueType);

	/** Expression evaluator that can be used to perform complex text filter queries */
	FTextFilterExpressionEvaluator TextFilterExpressionEvaluator;

	/** Transient context data, used when calling PassesFilter. Kept around to minimize re-allocations between multiple calls to PassesFilter */
	TArray<TSharedRef<FSequencerTextFilterExpressionContext>> TextFilterExpressionContexts;
};
