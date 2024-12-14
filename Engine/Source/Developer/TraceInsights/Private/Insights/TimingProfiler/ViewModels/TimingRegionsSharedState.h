// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Templates/SharedPointer.h"

// TraceInsights
#include "Insights/ITimingViewExtender.h"

namespace TraceServices
{
	class IAnalysisSession;
}

namespace UE::Insights::TimingProfiler
{

class FTimingRegionsTrack;
class STimingView;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingRegionsSharedState : public Timing::ITimingViewExtender, public TSharedFromThis<FTimingRegionsSharedState>
{
	friend class FTimingRegionsTrack;

public:
	explicit FTimingRegionsSharedState(STimingView* InTimingView);
	virtual ~FTimingRegionsSharedState() override = default;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Timing::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Timing::ITimingViewSession& InSession) override;
	virtual void Tick(Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendOtherTracksFilterMenu(Timing::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder) override;

	//////////////////////////////////////////////////

	void BindCommands();

	bool IsRegionsTrackVisible() const { return bShowHideRegionsTrack; }
	void ShowHideRegionsTrack();

private:
	STimingView* TimingView = nullptr;

	TSharedPtr<FTimingRegionsTrack> TimingRegionsTrack;

	bool bShowHideRegionsTrack = true;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
