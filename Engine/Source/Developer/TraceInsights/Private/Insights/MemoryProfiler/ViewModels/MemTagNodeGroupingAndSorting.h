// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Templates/SharedPointer.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/TableColumn.h"
#include "InsightsCore/Table/ViewModels/TableCellValueSorter.h"
#include "InsightsCore/Table/ViewModels/TreeNodeGrouping.h"

// TraceInsights
#include "Insights/MemoryProfiler/ViewModels/MemTagNode.h"

namespace UE::Insights::MemoryProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorters
////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemTagNodeSortingByType: public FTableCellValueSorter
{
public:
	FMemTagNodeSortingByType(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemTagNodeSortingByTracker : public FTableCellValueSorter
{
public:
	FMemTagNodeSortingByTracker(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemTagNodeSortingByInstanceCount : public FTableCellValueSorter
{
public:
	FMemTagNodeSortingByInstanceCount(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemTagNodeSortingByTotalInclusiveSize : public FTableCellValueSorter
{
public:
	FMemTagNodeSortingByTotalInclusiveSize(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemTagNodeSortingByTotalExclusiveSize : public FTableCellValueSorter
{
public:
	FMemTagNodeSortingByTotalExclusiveSize(TSharedRef<FTableColumn> InColumnRef);

	virtual void Sort(TArray<FBaseTreeNodePtr>& NodesToSort, ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Organizers
////////////////////////////////////////////////////////////////////////////////////////////////////

/** Enumerates types of grouping or sorting for the LLM tag nodes. */
enum class EMemTagNodeGroupingMode
{
	/** Creates a single group for all LLM tags. */
	Flat,

	/** Creates one group for one letter. */
	ByName,

	/** Creates one group for each event type. */
	ByType,

	/** Creates one group for each tracker. */
	ByTracker,

	/** Group LLM tags by their hierarchy. */
	ByParent,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

/** Type definition for shared pointers to instances of EMemTagNodeGroupingMode. */
typedef TSharedPtr<EMemTagNodeGroupingMode> EMemTagNodeGroupingModePtr;

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler
