// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// TraceServices
#include "TraceServices/Model/Memory.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/BaseTreeNode.h"

// TraceInsights
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTracker.h"

namespace TraceServices
{
	struct FMemoryProfilerAggregatedStats
	{
		uint32 Type;
		uint32 InstanceCount = 0U;
		uint64 Min = 0U;
		uint64 Max = 0U;
		uint64 Average = 0U;
		//uint64 Median = 0U;
	};
}

namespace UE::Insights::MemoryProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EMemTagNodeType
{
	/** The MemTagNode is a Low Level Memory Tag. */
	MemTag,

	/** The MemTagNode is a group node. */
	Group,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemTagNode;

/** Type definition for shared pointers to instances of FMemTagNode. */
typedef TSharedPtr<class FMemTagNode> FMemTagNodePtr;

/** Type definition for shared references to instances of FMemTagNode. */
typedef TSharedRef<class FMemTagNode> FMemTagNodeRef;

/** Type definition for shared references to const instances of FMemTagNode. */
typedef TSharedRef<const class FMemTagNode> FMemTagNodeRefConst;

/** Type definition for weak references to instances of FMemTagNode. */
typedef TWeakPtr<class FMemTagNode> FMemTagNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Class used to store information about an llm tag node (used in the SMemTagTreeView).
 */
class FMemTagNode : public FBaseTreeNode
{
	INSIGHTS_DECLARE_RTTI(FMemTagNode, FBaseTreeNode)

public:
	/** Initialization constructor for the MemTag node. */
	explicit FMemTagNode(FMemoryTag* InMemTag)
		: FBaseTreeNode(FName(InMemTag->GetStatFullName(), 0), false)
		, Type(EMemTagNodeType::MemTag)
		, MemTag(InMemTag)
	{
		ResetAggregatedStats();
	}

	/** Initialization constructor for the group node. */
	explicit FMemTagNode(const FName InGroupName)
		: FBaseTreeNode(InGroupName, true)
		, Type(EMemTagNodeType::Group)
		, MemTag(nullptr)
	{
		ResetAggregatedStats();
	}

	/**
	 * @return a type of this MemTag node or EMemTagNodeType::Group for group nodes.
	 */
	EMemTagNodeType GetType() const { return Type; }

	bool IsValidStat() const { return MemTag != nullptr; }
	FMemoryTag* GetMemTag() const { return MemTag; }

	FMemoryTagId GetMemTagId() const { return MemTag ? MemTag->GetId() : FMemoryTag::InvalidTagId; }

	FMemoryTrackerId GetMemTrackerId() const { return MemTag ? MemTag->GetTrackerId() : FMemoryTracker::InvalidTrackerId; }
	FText GetTrackerText() const;

	FLinearColor GetColor() const { return MemTag ? MemTag->GetColor() : FLinearColor(0.5f, 0.5f, 0.5f, 1.0f); }
	bool IsAddedToGraph() const { return MemTag ? MemTag->IsAddedToGraph() : false; }

	FMemTagNodePtr GetParentTagNode() const { return ParentTagNode; }
	FMemoryTag* GetParentMemTag() const { return ParentTagNode.IsValid() ? ParentTagNode->GetMemTag() : nullptr; }
	void SetParentTagNode(FMemTagNodePtr NodePtr) { ParentTagNode = NodePtr; }

	/**
	 * @return the aggregated stats for this MemTag node.
	 */
	const TraceServices::FMemoryProfilerAggregatedStats& GetAggregatedStats() const { return AggregatedStats; }

	void ResetAggregatedStats();
	//TODO: void SetAggregatedStats(const TraceServices::FMemoryProfilerAggregatedStats& AggregatedStats);

private:
	const EMemTagNodeType Type;
	FMemoryTag* MemTag;
	FMemTagNodePtr ParentTagNode;
	TraceServices::FMemoryProfilerAggregatedStats AggregatedStats;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler
