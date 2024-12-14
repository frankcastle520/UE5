// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "ProfilingDebugging/MiscTrace.h" // for ETraceFrameType

// TraceServices
#include "TraceServices/Containers/Tables.h"
#include "TraceServices/Model/TimingProfiler.h"

// TraceInsights
#include "Insights/TimingProfiler/ViewModels/StatsAggregator.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::Insights::TimingProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerAggregator : public FStatsAggregator
{
public:
	FTimerAggregator() : FStatsAggregator(TEXT("Timers")) {}
	virtual ~FTimerAggregator() {}

	TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>* GetResultTable() const;
	void ResetResults();

	ETraceFrameType GetFrameType() { return FrameType; }
	void SetFrameType(ETraceFrameType InFrameType) { FrameType = InFrameType; }

protected:
	virtual IStatsAggregationWorker* CreateWorker(TSharedPtr<const TraceServices::IAnalysisSession> InSession) override;

private:
	ETraceFrameType FrameType = ETraceFrameType::TraceFrameType_Count;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
