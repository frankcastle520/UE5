// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadTimingSharedState.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/LowLevelMemTracker.h"

// TraceServices
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Threads.h"

// TraceInsightsCore
#include "InsightsCore/Common/Log.h"

// TraceInsights
#include "Insights/InsightsStyle.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TimingProfiler/Tracks/ThreadTimingTrack.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "UE::Insights::TimingProfiler::ThreadTiming"

namespace UE::Insights::TimingProfiler
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FThreadTimingViewCommands
////////////////////////////////////////////////////////////////////////////////////////////////////

class FThreadTimingViewCommands : public TCommands<FThreadTimingViewCommands>
{
public:
	FThreadTimingViewCommands();
	virtual ~FThreadTimingViewCommands() {}
	virtual void RegisterCommands() override;

public:
	/** Toggles visibility for GPU thread track(s). */
	TSharedPtr<FUICommandInfo> ShowHideAllGpuTracks;

	/** Toggles visibility for all CPU thread tracks at once. */
	TSharedPtr<FUICommandInfo> ShowHideAllCpuTracks;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

FThreadTimingViewCommands::FThreadTimingViewCommands()
	: TCommands<FThreadTimingViewCommands>(
		TEXT("ThreadTimingViewCommands"),
		NSLOCTEXT("Contexts", "ThreadTimingViewCommands", "Insights - Timing View - Threads"),
		NAME_None,
		FInsightsStyle::GetStyleSetName())
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

// UI_COMMAND takes long for the compiler to optimize
UE_DISABLE_OPTIMIZATION_SHIP
void FThreadTimingViewCommands::RegisterCommands()
{
	UI_COMMAND(ShowHideAllGpuTracks,
		"GPU Track(s)",
		"Shows/hides the GPU track(s).",
		EUserInterfaceActionType::ToggleButton,
		FInputChord(EKeys::Y));

	UI_COMMAND(ShowHideAllCpuTracks,
		"CPU Thread Tracks",
		"Shows/hides all CPU tracks (and all CPU thread groups).",
		EUserInterfaceActionType::ToggleButton,
		FInputChord(EKeys::U));
}
UE_ENABLE_OPTIMIZATION_SHIP

////////////////////////////////////////////////////////////////////////////////////////////////////
// FThreadTimingSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

FThreadTimingSharedState::FThreadTimingSharedState(STimingView* InTimingView)
	: TimingView(InTimingView)
{
	check(TimingView != nullptr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FCpuTimingTrack> FThreadTimingSharedState::GetCpuTrack(uint32 InThreadId)
{
	TSharedPtr<FCpuTimingTrack>*const TrackPtrPtr = CpuTracks.Find(InThreadId);
	return TrackPtrPtr ? *TrackPtrPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::IsGpuTrackVisible() const
{
	return (GpuTrack != nullptr && GpuTrack->IsVisible()) || (Gpu2Track != nullptr && Gpu2Track->IsVisible());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::IsCpuTrackVisible(uint32 InThreadId) const
{
	const TSharedPtr<FCpuTimingTrack>*const TrackPtrPtr = CpuTracks.Find(InThreadId);
	return TrackPtrPtr && (*TrackPtrPtr)->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::GetVisibleCpuThreads(TSet<uint32>& OutSet) const
{
	OutSet.Reset();
	for (const auto& KV : CpuTracks)
	{
		const FCpuTimingTrack& Track = *KV.Value;
		if (Track.IsVisible())
		{
			OutSet.Add(KV.Key);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::GetVisibleTimelineIndexes(TSet<uint32>& OutSet) const
{
	OutSet.Reset();
	for (const auto& KV : CpuTracks)
	{
		const FCpuTimingTrack& Track = *KV.Value;
		if (Track.IsVisible())
		{
			OutSet.Add(Track.GetTimelineIndex());
		}
	}

	if (GpuTrack.IsValid() && GpuTrack->IsVisible())
	{
		OutSet.Add(GpuTrack->GetTimelineIndex());
	}

	if (Gpu2Track.IsValid() && Gpu2Track->IsVisible())
	{
		OutSet.Add(Gpu2Track->GetTimelineIndex());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::OnBeginSession(Timing::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (TimingView->GetName() == FInsightsManagerTabs::LoadingProfilerTabId)
	{
		bShowHideAllGpuTracks = false;
		bShowHideAllCpuTracks = false;
	}
	else
	{
		bShowHideAllGpuTracks = true;
		bShowHideAllCpuTracks = true;
	}

	GpuTrack = nullptr;
	Gpu2Track = nullptr;
	CpuTracks.Reset();
	ThreadGroups.Reset();

	TimingProfilerTimelineCount = 0;
	LoadTimeProfilerTimelineCount = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::OnEndSession(Timing::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	bShowHideAllGpuTracks = false;
	bShowHideAllCpuTracks = false;

	GpuTrack = nullptr;
	Gpu2Track = nullptr;
	CpuTracks.Reset();
	ThreadGroups.Reset();

	TimingProfilerTimelineCount = 0;
	LoadTimeProfilerTimelineCount = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::Tick(Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	const TraceServices::ITimingProfilerProvider* TimingProfilerProvider = TraceServices::ReadTimingProfilerProvider(InAnalysisSession);
	const TraceServices::ILoadTimeProfilerProvider* LoadTimeProfilerProvider = TraceServices::ReadLoadTimeProfilerProvider(InAnalysisSession);

	if (TimingProfilerProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

		const uint64 CurrentTimingProfilerTimelineCount = TimingProfilerProvider->GetTimelineCount();
		const uint64 CurrentLoadTimeProfilerTimelineCount = (LoadTimeProfilerProvider) ? LoadTimeProfilerProvider->GetTimelineCount() : 0;

		if (CurrentTimingProfilerTimelineCount != TimingProfilerTimelineCount ||
			CurrentLoadTimeProfilerTimelineCount != LoadTimeProfilerTimelineCount)
		{
			TimingProfilerTimelineCount = CurrentTimingProfilerTimelineCount;
			LoadTimeProfilerTimelineCount = CurrentLoadTimeProfilerTimelineCount;

			LLM_SCOPE_BYTAG(Insights);

			// Check if we have a GPU track.
			if (!GpuTrack.IsValid())
			{
				uint32 GpuTimelineIndex;
				if (TimingProfilerProvider->GetGpuTimelineIndex(GpuTimelineIndex))
				{
					GpuTrack = MakeShared<FGpuTimingTrack>(*this, TEXT("GPU"), nullptr, GpuTimelineIndex, FGpuTimingTrack::Gpu1ThreadId);
					GpuTrack->SetOrder(FTimingTrackOrder::Gpu);
					GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);
					InSession.AddScrollableTrack(GpuTrack);
				}
			}
			if (!Gpu2Track.IsValid())
			{
				uint32 GpuTimelineIndex;
				if (TimingProfilerProvider->GetGpu2TimelineIndex(GpuTimelineIndex))
				{
					Gpu2Track = MakeShared<FGpuTimingTrack>(*this, TEXT("GPU2"), nullptr, GpuTimelineIndex, FGpuTimingTrack::Gpu2ThreadId);
					Gpu2Track->SetOrder(FTimingTrackOrder::Gpu + 1);
					Gpu2Track->SetVisibilityFlag(bShowHideAllGpuTracks);
					InSession.AddScrollableTrack(Gpu2Track);
				}
			}

			bool bTracksOrderChanged = false;
			int32 Order = FTimingTrackOrder::Cpu;

			// Iterate through threads.
			const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(InAnalysisSession);
			ThreadProvider.EnumerateThreads([this, &InSession, &bTracksOrderChanged, &Order, TimingProfilerProvider, LoadTimeProfilerProvider](const TraceServices::FThreadInfo& ThreadInfo)
			{
				// Check if this thread is part of a group?
				bool bIsGroupVisible = bShowHideAllCpuTracks;
				const TCHAR* GroupName = ThreadInfo.GroupName;
				if (!GroupName || *GroupName == 0)
				{
					GroupName = ThreadInfo.Name;
				}
				if (!GroupName || *GroupName == 0)
				{
					GroupName = TEXT("Other Threads");
				}
				if (!ThreadGroups.Contains(GroupName))
				{
					// Note: The GroupName pointer should be valid for the duration of the session.
					ThreadGroups.Add(GroupName, { GroupName, bIsGroupVisible, 0, Order });
				}
				else
				{
					FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
					bIsGroupVisible = ThreadGroup.bIsVisible;
					ThreadGroup.Order = Order;
				}

				// Check if there is an available Asset Loading track for this thread.
				bool bIsLoadingThread = false;
				uint32 LoadingTimelineIndex;
				if (LoadTimeProfilerProvider && LoadTimeProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, LoadingTimelineIndex))
				{
					bIsLoadingThread = true;
				}

				// Check if there is an available CPU track for this thread.
				uint32 CpuTimelineIndex;
				if (TimingProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, CpuTimelineIndex))
				{
					TSharedPtr<FCpuTimingTrack>* TrackPtrPtr = CpuTracks.Find(ThreadInfo.Id);
					if (TrackPtrPtr == nullptr)
					{
						FString TrackName(ThreadInfo.Name && *ThreadInfo.Name ? ThreadInfo.Name : FString::Printf(TEXT("Thread %u"), ThreadInfo.Id));

						// Create new Timing Events track for the CPU thread.
						TSharedPtr<FCpuTimingTrack> Track = MakeShared<FCpuTimingTrack>(*this, TrackName, GroupName, CpuTimelineIndex, ThreadInfo.Id);
						Track->SetOrder(Order);
						CpuTracks.Add(ThreadInfo.Id, Track);

						FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
						ThreadGroup.NumTimelines++;

						if (bIsLoadingThread &&
							TimingView->GetName() == FInsightsManagerTabs::LoadingProfilerTabId)
						{
							Track->SetVisibilityFlag(true);
							ThreadGroup.bIsVisible = true;
						}
						else
						{
							Track->SetVisibilityFlag(bIsGroupVisible);
						}

						InSession.AddScrollableTrack(Track);
					}
					else
					{
						TSharedPtr<FCpuTimingTrack> Track = *TrackPtrPtr;
						if (Track->GetOrder() != Order)
						{
							Track->SetOrder(Order);
							bTracksOrderChanged = true;
						}
					}
				}

				constexpr int32 OrderIncrement = FTimingTrackOrder::GroupRange / 1000; // distribute max 1000 tracks in the order group range
				static_assert(OrderIncrement >= 1, "Order group range too small");
				Order += OrderIncrement;
			});

			if (bTracksOrderChanged)
			{
				InSession.InvalidateScrollableTracksOrder();
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ExtendGpuTracksFilterMenu(Timing::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InOutMenuBuilder.BeginSection("GpuTracks", LOCTEXT("ContextMenu_Section_GpuTracks", "GPU Tracks"));
	{
		InOutMenuBuilder.AddMenuEntry(FThreadTimingViewCommands::Get().ShowHideAllGpuTracks);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ExtendCpuTracksFilterMenu(Timing::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InOutMenuBuilder.BeginSection("CpuTracks", LOCTEXT("ContextMenu_Section_CpuTracks", "CPU Tracks"));
	{
		InOutMenuBuilder.AddMenuEntry(FThreadTimingViewCommands::Get().ShowHideAllCpuTracks);
	}
	InOutMenuBuilder.EndSection();

	InOutMenuBuilder.BeginSection("CpuThreadGroups", LOCTEXT("ContextMenu_Section_CpuThreadGroups", "CPU Thread Groups"));
	CreateThreadGroupsMenu(InOutMenuBuilder);
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::BindCommands()
{
	FThreadTimingViewCommands::Register();

	TSharedPtr<FUICommandList> CommandList = TimingView->GetCommandList();
	ensure(CommandList.IsValid());

	CommandList->MapAction(
		FThreadTimingViewCommands::Get().ShowHideAllGpuTracks,
		FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ShowHideAllGpuTracks),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::IsAllGpuTracksToggleOn));

	CommandList->MapAction(
		FThreadTimingViewCommands::Get().ShowHideAllCpuTracks,
		FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ShowHideAllCpuTracks),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::IsAllCpuTracksToggleOn));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::CreateThreadGroupsMenu(FMenuBuilder& InOutMenuBuilder)
{
	// Sort the list of thread groups.
	TArray<const FThreadGroup*> SortedThreadGroups;
	SortedThreadGroups.Reserve(ThreadGroups.Num());
	for (const auto& KV : ThreadGroups)
	{
		SortedThreadGroups.Add(&KV.Value);
	}
	Algo::SortBy(SortedThreadGroups, &FThreadGroup::GetOrder);

	for (const FThreadGroup* ThreadGroupPtr : SortedThreadGroups)
	{
		const FThreadGroup& ThreadGroup = *ThreadGroupPtr;
		if (ThreadGroup.NumTimelines > 0)
		{
			InOutMenuBuilder.AddMenuEntry(
				//FText::FromString(ThreadGroup.Name),
				FText::Format(LOCTEXT("ThreadGroupFmt", "{0} ({1})"), FText::FromString(ThreadGroup.Name), ThreadGroup.NumTimelines),
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ToggleTrackVisibilityByGroup_Execute, ThreadGroup.Name),
						  FCanExecuteAction::CreateLambda([] { return true; }),
						  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::ToggleTrackVisibilityByGroup_IsChecked, ThreadGroup.Name)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::SetAllCpuTracksToggle(bool bOnOff)
{
	bShowHideAllCpuTracks = bOnOff;

	for (const auto& KV : CpuTracks)
	{
		FCpuTimingTrack& Track = *KV.Value;
		Track.SetVisibilityFlag(bShowHideAllCpuTracks);
	}

	for (auto& KV : ThreadGroups)
	{
		KV.Value.bIsVisible = bShowHideAllCpuTracks;
	}

	TimingView->HandleTrackVisibilityChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::SetAllGpuTracksToggle(bool bOnOff)
{
	bShowHideAllGpuTracks = bOnOff;

	if (GpuTrack.IsValid())
	{
		GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);
	}
	if (Gpu2Track.IsValid())
	{
		Gpu2Track->SetVisibilityFlag(bShowHideAllGpuTracks);
	}
	if (GpuTrack.IsValid() || Gpu2Track.IsValid())
	{
		TimingView->HandleTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const
{
	if (ThreadGroups.Contains(InGroupName))
	{
		const FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		return ThreadGroup.bIsVisible;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName)
{
	if (ThreadGroups.Contains(InGroupName))
	{
		FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		ThreadGroup.bIsVisible = !ThreadGroup.bIsVisible;

		for (const auto& KV : CpuTracks)
		{
			FCpuTimingTrack& Track = *KV.Value;
			if (Track.GetGroupName() == InGroupName)
			{
				Track.SetVisibilityFlag(ThreadGroup.bIsVisible);
			}
		}

		TimingView->HandleTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<const ITimingEvent> FThreadTimingSharedState::FindMaxEventInstance(uint32 TimerId, double StartTime, double EndTime)
{
	auto CompareAndAssignEvent = [](TSharedPtr<const ITimingEvent>& TimingEvent, TSharedPtr<const ITimingEvent>& TrackEvent)
	{
		if (!TrackEvent.IsValid())
		{
			return;
		}

		if (!TimingEvent.IsValid() || TrackEvent->GetDuration() > TimingEvent->GetDuration())
		{
			TimingEvent = TrackEvent;
		}
	};

	TSharedPtr<const ITimingEvent> TimingEvent;
	TSharedPtr<const ITimingEvent> TrackEvent;

	for (const auto& KV : CpuTracks)
	{
		const FCpuTimingTrack& Track = *KV.Value;
		if (Track.IsVisible())
		{
			TrackEvent = Track.FindMaxEventInstance(TimerId, StartTime, EndTime);
			CompareAndAssignEvent(TimingEvent, TrackEvent);
		}
	}

	if (GpuTrack.IsValid() && GpuTrack->IsVisible())
	{
		TrackEvent = GpuTrack->FindMaxEventInstance(TimerId, StartTime, EndTime);
		CompareAndAssignEvent(TimingEvent, TrackEvent);
	}

	if (Gpu2Track.IsValid() && Gpu2Track->IsVisible())
	{
		TrackEvent = Gpu2Track->FindMaxEventInstance(TimerId, StartTime, EndTime);
		CompareAndAssignEvent(TimingEvent, TrackEvent);
	}

	return TimingEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<const ITimingEvent> FThreadTimingSharedState::FindMinEventInstance(uint32 TimerId, double StartTime, double EndTime)
{
	auto CompareAndAssignEvent = [](TSharedPtr<const ITimingEvent>& TimingEvent, TSharedPtr<const ITimingEvent>& TrackEvent)
	{
		if (!TrackEvent.IsValid())
		{
			return;
		}

		if (!TimingEvent.IsValid() || TrackEvent->GetDuration() < TimingEvent->GetDuration())
		{
			TimingEvent = TrackEvent;
		}
	};

	TSharedPtr<const ITimingEvent> TimingEvent;
	TSharedPtr<const ITimingEvent> TrackEvent;

	for (const auto& KV : CpuTracks)
	{
		const FCpuTimingTrack& Track = *KV.Value;
		if (Track.IsVisible())
		{
			TrackEvent = Track.FindMinEventInstance(TimerId, StartTime, EndTime);
			CompareAndAssignEvent(TimingEvent, TrackEvent);
		}
	}

	if (GpuTrack.IsValid() && GpuTrack->IsVisible())
	{
		TrackEvent = GpuTrack->FindMinEventInstance(TimerId, StartTime, EndTime);
		CompareAndAssignEvent(TimingEvent, TrackEvent);
	}

	if (Gpu2Track.IsValid() && Gpu2Track->IsVisible())
	{
		TrackEvent = Gpu2Track->FindMinEventInstance(TimerId, StartTime, EndTime);
		CompareAndAssignEvent(TimingEvent, TrackEvent);
	}

	return TimingEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler

#undef LOCTEXT_NAMESPACE
