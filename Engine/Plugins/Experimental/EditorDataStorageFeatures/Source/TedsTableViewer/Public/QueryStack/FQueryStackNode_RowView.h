// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "QueryStack/IQueryStackNode_Row.h"

namespace UE::Editor::DataStorage
{
	/*
	 * A very simple row query stack that views a list of rows.
	 * Note: The user is responsible for the lifetime of the Rows array (similar to SListView) and MarkDirty must be called to update
	 * the table viewer when the row list changes
	 */
	class FQueryStackNode_RowView : public IQueryStackNode_Row
	{
	public:
		// IQueryStackNode_Row interface
		TEDSTABLEVIEWER_API virtual TConstArrayView<RowHandle> GetOrderedRowList() override;
		TEDSTABLEVIEWER_API virtual uint32 GetRevisionId() const override;
		// IQueryStackNode_Row interface

		TEDSTABLEVIEWER_API FQueryStackNode_RowView(TArray<RowHandle>* InRows);

		// Increment the revision ID to update the table viewer when the list of rows changes
		TEDSTABLEVIEWER_API void MarkDirty();

	private:

		TArray<RowHandle>* Rows;
		uint32 RevisionId = 0;
	};
} // namespace UE::Editor::DataStorage
