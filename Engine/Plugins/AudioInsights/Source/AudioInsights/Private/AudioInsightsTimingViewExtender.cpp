// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioInsightsTimingViewExtender.h"

#include "Insights/ITimingViewSession.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace UE::Audio::Insights
{
	void FAudioInsightsTimingViewExtender::OnBeginSession(UE::Insights::Timing::ITimingViewSession& InSession)
	{
		InSession.OnTimeMarkerChanged().AddRaw(this, &FAudioInsightsTimingViewExtender::OnTimeMarkerChanged);
	}

	void FAudioInsightsTimingViewExtender::OnEndSession(UE::Insights::Timing::ITimingViewSession& InSession)
	{
		InSession.OnTimeMarkerChanged().RemoveAll(this);
	}

	void FAudioInsightsTimingViewExtender::Tick(UE::Insights::Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
	{
		AnalysisSession = &InAnalysisSession;
	}

	void FAudioInsightsTimingViewExtender::OnTimeMarkerChanged(UE::Insights::Timing::ETimeChangedFlags InFlags, double InTimeMarker)
	{
		if (AnalysisSession == nullptr)
		{
			return;
		}
		
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		const bool bIsValidTime = InTimeMarker >= 0.0 && InTimeMarker <= AnalysisSession->GetDurationSeconds();

		// Scrub Audio Insights when all data is loaded in non-live sessions
		if (AnalysisSession->IsAnalysisComplete() && bIsValidTime)
		{
			OnTimingViewTimeMarkerChanged.Broadcast(InTimeMarker);
		}
	}
} // namespace UE::Audio::Insights
