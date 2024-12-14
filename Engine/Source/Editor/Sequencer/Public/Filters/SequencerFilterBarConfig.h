// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SBasicFilterBar.h"
#include "SequencerFilterBarConfig.generated.h"

USTRUCT()
struct FSequencerFilterSet
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Label;

	/** Enabled and active states of common filters. Enabled if in the map. Active if the value of the key is true. */
	UPROPERTY()
	TMap<FString, bool> EnabledStates;

	UPROPERTY()
	FString TextFilterString;
};

USTRUCT()
struct FSequencerFilterBarConfig
{
	GENERATED_BODY()

public:
	/** Common Filters */

	bool IsFilterEnabled(const FString& InFilterName) const;
	bool SetFilterEnabled(const FString& InFilterName, const bool bInActive);

	bool IsFilterActive(const FString& InFilterName) const;
	bool SetFilterActive(const FString& InFilterName, const bool bInActive);

	const FSequencerFilterSet& GetCommonActiveSet() const;

	/** Custom Text Filters */

	TArray<FCustomTextFilterData>& GetCustomTextFilters();
	bool HasCustomTextFilter(const FString& InFilterName) const;
	FCustomTextFilterData* FindCustomTextFilter(const FString& InFilterName);
	bool AddCustomTextFilter(FCustomTextFilterData InFilterData);
	bool RemoveCustomTextFilter(const FString& InFilterName);

	/** Filter Bar Layout */

	EFilterBarLayout GetFilterBarLayout() const;
	void SetFilterBarLayout(const EFilterBarLayout InLayout);

protected:
	/** The currently active set of common and custom text filters that should be restored on editor load */
	UPROPERTY()
	FSequencerFilterSet ActiveFilters;

	/** User created custom text filters */
	UPROPERTY()
	TArray<FCustomTextFilterData> CustomTextFilters;

	/** The layout style for the filter bar widget */
	UPROPERTY()
	EFilterBarLayout FilterBarLayout = EFilterBarLayout::Vertical;
};
