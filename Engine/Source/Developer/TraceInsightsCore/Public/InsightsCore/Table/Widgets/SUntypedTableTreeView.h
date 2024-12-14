// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Input/Reply.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"

// TraceInsightsCore
#include "InsightsCore/Common/Stopwatch.h"
#include "InsightsCore/Table/ViewModels/UntypedTable.h"
#include "InsightsCore/Table/Widgets/STableTreeView.h"

namespace TraceServices
{
	class IUntypedTable;
}

namespace UE::Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTSCORE_API SUntypedTableTreeView : public STableTreeView
{
public:
	SUntypedTableTreeView();
	virtual ~SUntypedTableTreeView();

	SLATE_BEGIN_ARGS(SUntypedTableTreeView)
		: _RunInAsyncMode(false)
		{}
		SLATE_ARGUMENT(bool, RunInAsyncMode)
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FUntypedTable> InTablePtr);

	TSharedPtr<FUntypedTable> GetUntypedTable() const { return StaticCastSharedPtr<FUntypedTable>(GetTable()); }

	void UpdateSourceTable(TSharedPtr<TraceServices::IUntypedTable> SourceTable);

	virtual void Reset();

	//////////////////////////////////////////////////
	// IAsyncOperationStatusProvider

	virtual bool IsRunning() const override;
	virtual double GetAllOperationsDuration() override;
	virtual double GetCurrentOperationDuration() override { return 0.0; }
	virtual uint32 GetOperationCount() const override { return 1; }
	virtual FText GetCurrentOperationName() const override;

	//////////////////////////////////////////////////

	void SetCurrentOperationNameOverride(const FText& InOperationName);
	void ClearCurrentOperationNameOverride();

	/**
	 * Rebuilds the tree (if necessary).
	 * @param bResync - If true, it forces a resync even if the list did not changed since last sync.
	 */
	virtual void RebuildTree(bool bResync);

private:
	FStopwatch CurrentOperationStopwatch;
	FText CurrentOperationNameOverride;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights
