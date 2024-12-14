// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace TraceServices
{
	struct FTimeRegion;
}

// TraceInsights
#include "Insights/ViewModels/TimingEventsTrack.h"

namespace UE::Insights { class FFilterConfigurator; }

namespace UE::Insights::TimingProfiler
{

class FTimingRegionsSharedState;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingRegionsTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FTimingRegionsTrack, FTimingEventsTrack)

public:
	explicit FTimingRegionsTrack(FTimingRegionsSharedState& InSharedState)
		: FTimingEventsTrack(TEXT("Timing Regions"))
		, SharedState(InSharedState)
	{
	}

	virtual ~FTimingRegionsTrack() override;

	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void SetFilterConfigurator(TSharedPtr<FFilterConfigurator> InFilterConfigurator) override;
	virtual bool HasCustomFilter() const override;
	virtual void OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const override;

protected:
	bool FindRegionEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const TraceServices::FTimeRegion&)> InFoundPredicate) const;

private:
	FTimingRegionsSharedState& SharedState;

	TSharedPtr<FFilterConfigurator> FilterConfigurator;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
