// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Templates/SharedPointer.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/Table.h"

namespace TraceServices
{
	class ITableLayout;
	class IUntypedTable;
	class IUntypedTableReader;
}

namespace UE::Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API FUntypedTable : public FTable
{
public:
	FUntypedTable();
	virtual ~FUntypedTable();

	virtual void Reset();

	TSharedPtr<TraceServices::IUntypedTable> GetSourceTable() const { return SourceTable; }
	TSharedPtr<TraceServices::IUntypedTableReader> GetTableReader() const { return TableReader; }

	/* Update table content. Returns true if the table layout has changed. */
	bool UpdateSourceTable(TSharedPtr<TraceServices::IUntypedTable> InSourceTable);

private:
	void CreateColumns(const TraceServices::ITableLayout& TableLayout);

private:
	TSharedPtr<TraceServices::IUntypedTable> SourceTable;
	TSharedPtr<TraceServices::IUntypedTableReader> TableReader;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
