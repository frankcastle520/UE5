// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Filters/SequencerTrackFilterBase.h"
#include "Misc/FilterCollection.h"
#include "Misc/IFilter.h"

class FSequencer;

/** Some of this classes functionality could be moved to the TFilterCollection base class so other places could take advantage */
class FSequencerTrackFilterCollection : public TFilterCollection<FSequencerTrackFilterType>
{
public:
	FSequencerTrackFilterCollection(ISequencerTrackFilters& InFilterInterface);
	virtual ~FSequencerTrackFilterCollection() {};

	// @todo Maybe this should get moved in to TFilterCollection
	bool ContainsFilter(const TSharedRef<FSequencerTrackFilter>& InItem) const;

	// @todo Maybe this should get moved in to TFilterCollection
	void RemoveAll();

	/**
	 * Adds the specified Filter to the collection
	 * 
	 * @param InFilter The filter object to add to the collection
	 * 
	 * @return The index in the collection at which the filter was added
	 */
	int32 Add(const TSharedRef<FSequencerTrackFilter>& InFilter);

	/**
	 * Removes as many instances of the specified Filter as there are in the collection
	 * 
	 * @param InFilter The filter object to remove from the collection
	 * 
	 * @return The number of Filters removed from the collection
	 */
	int32 Remove(const TSharedRef<FSequencerTrackFilter>& InFilter);

	/**
	 * Gets the filter at the specified index
	 * 
	 * @param InIndex The index of the requested filter in the ChildFilters array
	 * 
	 * @return Filter at the specified index
	 */
	TSharedRef<FSequencerTrackFilter> GetFilterAtIndex(const int32 InIndex);

	/** Returns the number of Filters in the collection */
	int32 Num() const;

	bool IsEmpty() const;

	/** Sorts the filters by display string */
	void Sort();

	/** Gets all the available track filter names */
	TArray<FText> GetFilterDisplayNames() const;

	TArray<TSharedRef<FSequencerTrackFilter>> GetAllFilters(const TArray<TSharedRef<FFilterCategory>>& InCategories = {}) const;

	TSet<TSharedRef<FFilterCategory>> GetCategories(const TSet<TSharedRef<FSequencerTrackFilter>>* InFilters = nullptr) const;

	/** @return List of filters that match the specified category */
	TArray<TSharedRef<FSequencerTrackFilter>> GetCategoryFilters(const TSharedRef<FFilterCategory>& InCategory) const;

	void ForEachFilter(const TFunctionRef<bool(const TSharedRef<FSequencerTrackFilter>&)>& InFunction
		, const bool bInCheckSupportsSequence
		, const TArray<TSharedRef<FFilterCategory>>& InCategories = {}) const;

protected:
	/**
	 *	Called when a child Filter restrictions change and broadcasts the FilterChanged delegate
	 *	for the collection
	 */
	void OnChildFilterChanged();

	ISequencerTrackFilters& FilterInterface;

public:
	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	FORCEINLINE TArray<TSharedPtr<IFilter<FSequencerTrackFilterType>>>::RangedForIteratorType      begin()       { return ChildFilters.begin(); }
	FORCEINLINE TArray<TSharedPtr<IFilter<FSequencerTrackFilterType>>>::RangedForConstIteratorType begin() const { return ChildFilters.begin(); }
	FORCEINLINE TArray<TSharedPtr<IFilter<FSequencerTrackFilterType>>>::RangedForIteratorType      end()	     { return ChildFilters.end(); }
	FORCEINLINE TArray<TSharedPtr<IFilter<FSequencerTrackFilterType>>>::RangedForConstIteratorType end()   const { return ChildFilters.end(); }
};
