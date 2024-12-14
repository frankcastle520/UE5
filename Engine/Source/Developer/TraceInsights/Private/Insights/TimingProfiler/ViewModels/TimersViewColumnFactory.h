// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

// TraceInsightsCore
#include "InsightsCore/Table/ViewModels/TableColumn.h"

namespace UE::Insights::TimingProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////

// Column identifiers
struct FTimersViewColumns
{
	static const FName NameColumnID;
	static const FName MetaGroupNameColumnID;
	static const FName TypeColumnID;
	static const FName InstanceCountColumnID;
	static const FName ChildInstanceCountColumnID;

	// Inclusive Time columns
	static const FName TotalInclusiveTimeColumnID;
	static const FName MaxInclusiveTimeColumnID;
	static const FName UpperQuartileInclusiveTimeColumnID;
	static const FName AverageInclusiveTimeColumnID;
	static const FName MedianInclusiveTimeColumnID;
	static const FName LowerQuartileInclusiveTimeColumnID;
	static const FName MinInclusiveTimeColumnID;

	// Exclusive Time columns
	static const FName TotalExclusiveTimeColumnID;
	static const FName MaxExclusiveTimeColumnID;
	static const FName UpperQuartileExclusiveTimeColumnID;
	static const FName AverageExclusiveTimeColumnID;
	static const FName MedianExclusiveTimeColumnID;
	static const FName LowerQuartileExclusiveTimeColumnID;
	static const FName MinExclusiveTimeColumnID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimersTableColumn : public FTableColumn
{
public:
	FTimersTableColumn(const FName InId)
		: FTableColumn(InId)
	{}

	FText GetDescription(ETraceFrameType InAggregationMode) const
	{
		switch (InAggregationMode)
		{
		case TraceFrameType_Game:
			return GameFrame_Description;
			break;
		case TraceFrameType_Rendering:
			return RenderingFrame_Description;
			break;
		default:
			return FTableColumn::GetDescription();
		}
	}

	void SetDescription(ETraceFrameType InAggregationMode, FText InDescription)
	{
		switch (InAggregationMode)
		{
		case TraceFrameType_Game:
			GameFrame_Description = InDescription;
			break;
		case TraceFrameType_Rendering:
			RenderingFrame_Description = InDescription;
			break;
		case TraceFrameType_Count:
			FTableColumn::SetDescription(InDescription);
			break;
		default:
			ensure(0);
		}
	}

private:
	FText GameFrame_Description;
	FText RenderingFrame_Description;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimersViewColumnFactory
{
public:
	static void CreateTimersViewColumns(TArray<TSharedRef<FTableColumn>>& Columns);
	static void CreateTimerTreeViewColumns(TArray<TSharedRef<FTableColumn>>& Columns);

	static TSharedRef<FTableColumn> CreateNameColumn();
	static TSharedRef<FTableColumn> CreateMetaGroupNameColumn();
	static TSharedRef<FTableColumn> CreateTypeColumn();
	static TSharedRef<FTableColumn> CreateInstanceCountColumn();
	static TSharedRef<FTableColumn> CreateChildInstanceCountColumn();

	static TSharedRef<FTableColumn> CreateTotalInclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateMaxInclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateAverageInclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateMedianInclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateMinInclusiveTimeColumn();

	static TSharedRef<FTableColumn> CreateTotalExclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateMaxExclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateAverageExclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateMedianExclusiveTimeColumn();
	static TSharedRef<FTableColumn> CreateMinExclusiveTimeColumn();

private:
	static constexpr float TotalTimeColumnInitialWidth = 60.0f;
	static constexpr float TimeMsColumnInitialWidth = 50.0f;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
