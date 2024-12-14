// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Elements/Common/TypedElementHandles.h"

namespace UE::Editor::DataStorage
{
	/*
	 * A row node in the query stack is used by the Teds Table Viewer to determine which rows to visualize in the table viewer
	 * @see FQueryStackNode_RowView for a simple implementation that views a list of rows input by the user
	 */
	class IQueryStackNode_Row
	{
	public:
		
		virtual ~IQueryStackNode_Row() = default;

		// Get the rows to visualize
		virtual TConstArrayView<RowHandle> GetOrderedRowList() = 0;

		// Returns an id that updates whenever the locally stored rows have changed.
		virtual uint32 GetRevisionId() const = 0;
	};
} // namespace UE::Editor::DataStorage
