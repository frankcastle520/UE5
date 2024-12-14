// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Model/TimingProfilerPrivate.h"

namespace TraceServices
{

	class FAnalysisSession;

class FGpuProfilerAnalyzer
	: public UE::Trace::IAnalyzer
{
public:
	FGpuProfilerAnalyzer(FAnalysisSession& Session, FTimingProfilerProvider& TimingProfilerProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_Frame, // GPU Index 0
		RouteId_Frame2, // GPU Index 1
	};

	FAnalysisSession& Session;
	FTimingProfilerProvider& TimingProfilerProvider;
	TMap<uint64, uint32> EventTypeMap;
	double MinTime = DBL_MIN;
	double MinTime2 = DBL_MIN;
	uint32 NumFrames = 0;
	uint32 NumFramesWithErrors = 0;
};

} // namespace TraceServices
