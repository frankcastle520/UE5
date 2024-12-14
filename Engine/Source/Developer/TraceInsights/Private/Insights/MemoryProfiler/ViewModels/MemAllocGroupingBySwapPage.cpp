// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocGroupingBySwapPage.h"

#include "Internationalization/Internationalization.h"

// TraceServices
#include "Common/ProviderLock.h"

// TraceInsightsCore
#include "InsightsCore/Common/AsyncOperationProgress.h"

// TraceInsights
#include "Insights/MemoryProfiler/ViewModels/CallstackFormatting.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocInSwapNode.h"

#define LOCTEXT_NAMESPACE "UE::Insights::MemoryProfiler::FMemAllocNode"

namespace UE::Insights::MemoryProfiler
{

INSIGHTS_IMPLEMENT_RTTI(FMemAllocGroupingBySwapPage)

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingBySwapPage::FMemAllocGroupingBySwapPage(const TraceServices::IAllocationsProvider& InAllocProvider)
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_BySwap_ShortName", "Swap"),
		LOCTEXT("Grouping_BySwap_TitleName", "By Swap"),
		LOCTEXT("Grouping_BySwap_Desc", "Creates a tree based on swap state."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
	, AllocProvider(InAllocProvider)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingBySwapPage::~FMemAllocGroupingBySwapPage()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

struct TMemoryPageMapKeyFuncs : BaseKeyFuncs<TPair<uint64, FTableTreeNode*>, uint64, false>
{
	static FORCEINLINE uint64 GetSetKey(const TPair<uint64, FTableTreeNode*>& Element)
	{
		return Element.Key;
	}
	static FORCEINLINE uint32 GetKeyHash(const uint64 Key)
	{
		// memory page is always at least 4k aligned, so skip lower bytes
		return (uint32)(Key >> 12);
	}
	static FORCEINLINE bool Matches(const uint64 A, const uint64 B)
	{
		return A == B;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemSwapPageTreeNode : public FTableTreeNode
{
	INSIGHTS_DECLARE_RTTI(FMemSwapPageTreeNode, FTableTreeNode)

public:
	/** Initialization constructor for the group node. */
	explicit FMemSwapPageTreeNode(const FName InName, TWeakPtr<FTable> InParentTable)
		: FTableTreeNode(InName, InParentTable)
	{
	}

	virtual ~FMemSwapPageTreeNode()
	{
	}

	virtual FLinearColor GetIconColor() const override final
	{
		return FLinearColor(0.3f, 0.8f, 0.4f, 1.0f);
	}

	virtual FLinearColor GetColor() const override final
	{
		return FLinearColor(0.2f, 0.8f, 0.4f, 1.0f);
	}
};

INSIGHTS_IMPLEMENT_RTTI(FMemSwapPageTreeNode)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemAllocGroupingBySwapPage::GroupNodes(
	const TArray<FTableTreeNodePtr>& Nodes,
	FTableTreeNode& ParentGroup,
	TWeakPtr<FTable> InParentTable,
	IAsyncOperationProgress& InAsyncOperationProgress) const
{
	ParentGroup.ClearChildren();

	auto InSwapGroup = MakeShared<FTableTreeNode>(FName(TEXT("In Swap")), InParentTable);
	auto InRamGroup = MakeShared<FTableTreeNode>(FName(TEXT("In RAM")), InParentTable);
	ParentGroup.AddChildAndSetParent(InSwapGroup);
	ParentGroup.AddChildAndSetParent(InRamGroup);

	TMap<uint64, FTableTreeNode*, FDefaultSetAllocator, TMemoryPageMapKeyFuncs> SwapNodes;

	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (InAsyncOperationProgress.ShouldCancelAsyncOp())
		{
			return;
		}

		const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(*NodePtr);
		const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();

		if (Alloc && Alloc->IsSwap())
		{
			auto SwapEntry = MakeShared<FMemSwapPageTreeNode>(FName(FString::Printf(TEXT("0x%016llx"), Alloc->GetAddress())), InParentTable);
			InSwapGroup->AddChildAndSetParent(SwapEntry);
			SwapEntry->AddChildAndSetParent(NodePtr);

			ensure(SwapNodes.Find(Alloc->GetAddress()) == nullptr);

			SwapNodes.Add(Alloc->GetAddress(), &SwapEntry.Get());
		}
	}

	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (InAsyncOperationProgress.ShouldCancelAsyncOp())
		{
			return;
		}

		if (NodePtr->IsGroup())
		{
			ParentGroup.AddChildAndSetParent(NodePtr);
			continue;
		}

		const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(*NodePtr);
		const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();

		if (!Alloc)
		{
			InRamGroup->AddChildAndSetParent(NodePtr);
			continue;
		}

		if (!Alloc->IsSwap())
		{
			bool bAnyInSwap = false;
			const uint64 PageSize = AllocProvider.GetPlatformPageSize();
			const uint64 PageMask = ~(PageSize - 1);

			const uint64 AllocStart = Alloc->GetAddress();
			const uint64 AllocEnd = AllocStart + FMath::Max(Alloc->GetSize(), 1); // fake alloc to have at least 1 byte size to make page walking logic work
			const uint64 PageRangeStart = AllocStart & PageMask;
			const uint64 PageRangeEnd = (AllocEnd + PageSize - 1) & PageMask;

			// for every alloc go through every memory page and check if it's in swap
			for (uint64 PageAddress = PageRangeStart; PageAddress < PageRangeEnd; PageAddress += PageSize)
			{
				FTableTreeNode** GroupNodePtr = SwapNodes.Find(PageAddress);
				if (!GroupNodePtr)
				{
					continue;
				}

				FTableTreeNode* GroupNode = *GroupNodePtr;

				uint64 AllocSizeInPage = PageSize;
				if (PageAddress < AllocStart) // adjust size if allocation starts after page start 
				{
					AllocSizeInPage -= AllocStart - PageAddress;
				}
				if (PageAddress + PageSize > AllocEnd) // adjust size if allocation ends before page end
				{
					AllocSizeInPage -= PageAddress + PageSize - AllocEnd;
				}

				auto SwapAllocNode = MakeShared<FMemAllocInSwapNode>(MemAllocNode.GetName(), StaticCastWeakPtr<FMemAllocTable>(MemAllocNode.GetParentTable()), MemAllocNode.GetRowIndex(), AllocSizeInPage);
				GroupNode->AddChildAndSetParent(SwapAllocNode);

				bAnyInSwap = true;
			}

			if (!bAnyInSwap)
			{
				InRamGroup->AddChildAndSetParent(NodePtr);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::MemoryProfiler

#undef LOCTEXT_NAMESPACE
