// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimingRegionsSharedState.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// TraceServices
#include "TraceServices/Model/AnalysisSession.h"

// TraceInsights
#include "Insights/Common/InsightsMenuBuilder.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TimingProfiler/Tracks/RegionsTimingTrack.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "UE::Insights::TimingProfiler::TimingRegions"

namespace UE::Insights::TimingProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingRegionsViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingRegionsViewCommands : public TCommands<FTimingRegionsViewCommands>
{
public:
	FTimingRegionsViewCommands();
	virtual ~FTimingRegionsViewCommands() {}
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> ShowHideTimingRegionsTrack;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingRegionsViewCommands::FTimingRegionsViewCommands()
	: TCommands<FTimingRegionsViewCommands>(
		TEXT("FTimingRegionsViewCommands"),
		NSLOCTEXT("Contexts", "FTimingRegionsViewCommands", "Insights - Timing View - Timing Regions"),
		NAME_None,
		FInsightsStyle::GetStyleSetName())
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compiler to optimize
UE_DISABLE_OPTIMIZATION_SHIP
void FTimingRegionsViewCommands::RegisterCommands()
{
	UI_COMMAND(ShowHideTimingRegionsTrack,
		"Timing Regions Track",
		"Shows/hides the Timing Regions track.",
		EUserInterfaceActionType::ToggleButton,
		FInputChord(EModifierKey::Control, EKeys::R));
}
UE_ENABLE_OPTIMIZATION_SHIP

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimingRegionsSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimingRegionsSharedState::FTimingRegionsSharedState(STimingView* InTimingView)
	: TimingView(InTimingView)
{
	check(TimingView != nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsSharedState::OnBeginSession(Timing::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	TimingRegionsTrack.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsSharedState::OnEndSession(Timing::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	TimingRegionsTrack.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsSharedState::Tick(Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (!TimingRegionsTrack.IsValid())
	{
		TimingRegionsTrack = MakeShared<FTimingRegionsTrack>(*this);
		TimingRegionsTrack->SetOrder(FTimingTrackOrder::First);
		TimingRegionsTrack->SetVisibilityFlag(true);
		InSession.AddScrollableTrack(TimingRegionsTrack);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsSharedState::ShowHideRegionsTrack()
{
	bShowHideRegionsTrack = !bShowHideRegionsTrack;

	if (TimingRegionsTrack.IsValid())
	{
		TimingRegionsTrack->SetVisibilityFlag(bShowHideRegionsTrack);
	}

	if (TimingView)
	{
		TimingView->HandleTrackVisibilityChanged();
	}

	if (bShowHideRegionsTrack)
	{
		TimingRegionsTrack->SetDirtyFlag();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsSharedState::ExtendOtherTracksFilterMenu(Timing::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	InOutMenuBuilder.BeginSection("Timing Regions", LOCTEXT("ContextMenu_Section_Regions", "Timing Regions"));
	{
		InOutMenuBuilder.AddMenuEntry(FTimingRegionsViewCommands::Get().ShowHideTimingRegionsTrack);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimingRegionsSharedState::BindCommands()
{
	FTimingRegionsViewCommands::Register();

	TSharedPtr<FUICommandList> CommandList = TimingView->GetCommandList();
	ensure(CommandList.IsValid());

	CommandList->MapAction(
		FTimingRegionsViewCommands::Get().ShowHideTimingRegionsTrack,
		FExecuteAction::CreateSP(this, &FTimingRegionsSharedState::ShowHideRegionsTrack),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FTimingRegionsSharedState::IsRegionsTrackVisible));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler

#undef LOCTEXT_NAMESPACE
