// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ITimingViewExtender.h"
#include "SlateTimingViewSession.h" // IWYU pragma: keep

namespace TraceServices { class IAnalysisSession; }
namespace UE::Insights::Timing { class ITimingViewSession; }

class FMenuBuilder;

namespace UE
{
namespace SlateInsights
{

class FSlateTimingViewExtender : public UE::Insights::Timing::ITimingViewExtender
{
public:
	//~ Begin UE::Insights::Timing::ITimingViewExtender interface
	virtual void OnBeginSession(UE::Insights::Timing::ITimingViewSession& InSession) override;
	virtual void OnEndSession(UE::Insights::Timing::ITimingViewSession& InSession) override;
	virtual void Tick(UE::Insights::Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(UE::Insights::Timing::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;
	//~ End UE::Insights::Timing::ITimingViewExtender interface

private:
	struct FPerSessionData
	{
		TUniquePtr<FSlateTimingViewSession> SharedData;
	};

	// The data we host per-session
	TMap<UE::Insights::Timing::ITimingViewSession*, FPerSessionData> PerSessionDataMap;
};

} //namespace SlateInsights
} //namespace UE
