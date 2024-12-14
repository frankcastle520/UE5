// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Delegates/Delegate.h"
#include "Insights/ITimingViewExtender.h"

namespace Insights
{
	class ITimingViewSession;
	enum class ETimeChangedFlags : int32;
}

class IAnalysisSession;

namespace UE::Audio::Insights
{
	class  FAudioInsightsTimingViewExtender : public UE::Insights::Timing::ITimingViewExtender
	{
	public:
		// Insights::ITimingViewExtender interface
		virtual void OnBeginSession(UE::Insights::Timing::ITimingViewSession& InSession) override;
		virtual void OnEndSession(UE::Insights::Timing::ITimingViewSession& InSession) override;
		virtual void Tick(UE::Insights::Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnTimingViewTimeMarkerChanged, double /*TimeMarker*/);
		FOnTimingViewTimeMarkerChanged OnTimingViewTimeMarkerChanged;

	private:
		void OnTimeMarkerChanged(UE::Insights::Timing::ETimeChangedFlags InFlags, double InTimeMarker);

		const TraceServices::IAnalysisSession* AnalysisSession = nullptr;
	};
} // namespace UE::Audio::Insights
