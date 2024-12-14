// Copyright Epic Games, Inc. All Rights Reserved.

#include "QueryStack/FQueryStackNode_RowView.h"

namespace UE::Editor::DataStorage
{
	TConstArrayView<RowHandle> FQueryStackNode_RowView::GetOrderedRowList()
	{
		return *Rows;
	}

	uint32 FQueryStackNode_RowView::GetRevisionId() const
	{
		return RevisionId;
	}

	FQueryStackNode_RowView::FQueryStackNode_RowView(TArray<RowHandle>* InRows)
		: Rows(InRows)
	{
		
	}

	void FQueryStackNode_RowView::MarkDirty()
	{
		++RevisionId;
	}
} // namespace UE::Editor::DataStorage
