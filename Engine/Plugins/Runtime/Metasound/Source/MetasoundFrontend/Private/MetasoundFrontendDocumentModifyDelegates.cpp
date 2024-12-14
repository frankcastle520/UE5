// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendDocumentModifyDelegates.h"


namespace Metasound::Frontend
{
	FDocumentModifyDelegates::FDocumentModifyDelegates()
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		: NodeDelegates()
		, EdgeDelegates()
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
	}

	FDocumentModifyDelegates::FDocumentModifyDelegates(const FDocumentModifyDelegates& InModifyDelegates)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		: NodeDelegates(InModifyDelegates.NodeDelegates)
		, EdgeDelegates(InModifyDelegates.EdgeDelegates)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		, PageNodeDelegates(InModifyDelegates.PageNodeDelegates)
		, PageEdgeDelegates(InModifyDelegates.PageEdgeDelegates)
	{
	}

	FDocumentModifyDelegates::FDocumentModifyDelegates(FDocumentModifyDelegates&& InModifyDelegates)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		: NodeDelegates(MoveTemp(InModifyDelegates.NodeDelegates))
		, EdgeDelegates(MoveTemp(InModifyDelegates.EdgeDelegates))
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		, PageNodeDelegates(MoveTemp(InModifyDelegates.PageNodeDelegates))
		, PageEdgeDelegates(MoveTemp(InModifyDelegates.PageEdgeDelegates))
	{
	}

	FDocumentModifyDelegates& FDocumentModifyDelegates::operator=(const FDocumentModifyDelegates& InModifyDelegates)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		NodeDelegates = InModifyDelegates.NodeDelegates;
		EdgeDelegates = InModifyDelegates.EdgeDelegates;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		PageNodeDelegates = InModifyDelegates.PageNodeDelegates;
		PageEdgeDelegates = InModifyDelegates.PageEdgeDelegates;
		return *this;
	}

	FDocumentModifyDelegates& FDocumentModifyDelegates::operator=(FDocumentModifyDelegates&& InModifyDelegates)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		NodeDelegates = MoveTemp(InModifyDelegates.NodeDelegates);
		EdgeDelegates = MoveTemp(InModifyDelegates.EdgeDelegates);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		PageNodeDelegates = MoveTemp(InModifyDelegates.PageNodeDelegates);
		PageEdgeDelegates = MoveTemp(InModifyDelegates.PageEdgeDelegates);
		return *this;
	}

	FDocumentModifyDelegates::FDocumentModifyDelegates(const FMetasoundFrontendDocument& Document)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		: NodeDelegates()
		, EdgeDelegates()
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		Document.RootGraph.IterateGraphPages([this](const FMetasoundFrontendGraph& Graph)
		{
			AddPageDelegates(Graph.PageID);
		});
	}

	void FDocumentModifyDelegates::AddPageDelegates(const FGuid& InPageID)
	{
		PageNodeDelegates.Add(InPageID, FNodeModifyDelegates());
		PageEdgeDelegates.Add(InPageID, FEdgeModifyDelegates());

		PageDelegates.OnPageAdded.Broadcast(FDocumentMutatePageArgs{ InPageID });
	}

	FNodeModifyDelegates& FDocumentModifyDelegates::FindNodeDelegatesChecked(const FGuid& InPageID)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return InPageID == DefaultPageID
		? NodeDelegates
		: PageNodeDelegates.FindChecked(InPageID);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	FEdgeModifyDelegates& FDocumentModifyDelegates::FindEdgeDelegatesChecked(const FGuid& InPageID)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return InPageID == DefaultPageID
		? EdgeDelegates
		: PageEdgeDelegates.FindChecked(InPageID);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	void FDocumentModifyDelegates::RemovePageDelegates(const FGuid& InPageID)
	{
		PageDelegates.OnRemovingPage.Broadcast(FDocumentMutatePageArgs { InPageID });

		PageNodeDelegates.Remove(InPageID);
		PageEdgeDelegates.Remove(InPageID);
	}
} // namespace Metasound::Frontend
