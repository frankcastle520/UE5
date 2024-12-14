// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/BaseTreeNode.h"
#include "InsightsCore/Table/ViewModels/TreeNodeGrouping.h"

namespace TraceServices { class IAllocationsProvider; }

namespace UE::Insights { class IAsyncOperationProgress; }

namespace UE::Insights::MemoryProfiler
{

class FMemAllocGroupingBySwapPage : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FMemAllocGroupingBySwapPage, FTreeNodeGrouping)

public:
	FMemAllocGroupingBySwapPage(const TraceServices::IAllocationsProvider& AllocProvider);
	virtual ~FMemAllocGroupingBySwapPage() override;

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, IAsyncOperationProgress& InAsyncOperationProgress) const override;

private:
	const TraceServices::IAllocationsProvider& AllocProvider;
};

} // namespace UE::Insights::MemoryProfiler
