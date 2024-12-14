// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Containers/ContainerAllocationPolicies.h" // for FDefaultSetAllocator
#include "Containers/Map.h"
#include "Misc/Crc.h" // for TStringPointerMapKeyFuncs_DEPRECATED
#include "Templates/SharedPointer.h"

// TraceInsights
#include "Insights/ITimingViewExtender.h"

namespace TraceServices
{
	class IAnalysisSession;
}

namespace UE::Insights::TimingProfiler
{

class FCpuTimingTrack;
class FGpuTimingTrack;
class STimingView;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FThreadTimingSharedState : public Timing::ITimingViewExtender, public TSharedFromThis<FThreadTimingSharedState>
{
private:
	struct FThreadGroup
	{
		const TCHAR* Name; /**< The thread group name; pointer to string owned by ThreadProvider. */
		bool bIsVisible;  /**< Toggle to show/hide all thread timelines associated with this group at once. Used also as default for new thread timelines. */
		uint32 NumTimelines; /**< Number of thread timelines associated with this group. */
		int32 Order; //**< Order index used for sorting. Inherited from last thread timeline associated with this group. **/

		int32 GetOrder() const { return Order; }
	};

public:
	explicit FThreadTimingSharedState(STimingView* InTimingView);
	virtual ~FThreadTimingSharedState() override = default;

	TSharedPtr<FGpuTimingTrack> GetGpuTrack() { return GpuTrack; }
	TSharedPtr<FGpuTimingTrack> GetGpu2Track() { return Gpu2Track; }
	TSharedPtr<FCpuTimingTrack> GetCpuTrack(uint32 InThreadId);
	const TMap<uint32, TSharedPtr<FCpuTimingTrack>> GetAllCpuTracks() { return CpuTracks; }

	bool IsGpuTrackVisible() const;
	bool IsCpuTrackVisible(uint32 InThreadId) const;

	void GetVisibleCpuThreads(TSet<uint32>& OutSet) const;
	void GetVisibleTimelineIndexes(TSet<uint32>& OutSet) const;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Timing::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Timing::ITimingViewSession& InSession) override;
	virtual void Tick(Timing::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendGpuTracksFilterMenu(Timing::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;
	virtual void ExtendCpuTracksFilterMenu(Timing::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	void BindCommands();

	bool IsAllGpuTracksToggleOn() const { return bShowHideAllGpuTracks; }
	void SetAllGpuTracksToggle(bool bOnOff);
	void ShowAllGpuTracks() { SetAllGpuTracksToggle(true); }
	void HideAllGpuTracks() { SetAllGpuTracksToggle(false); }
	void ShowHideAllGpuTracks() { SetAllGpuTracksToggle(!IsAllGpuTracksToggleOn()); }

	bool IsAllCpuTracksToggleOn() const { return bShowHideAllCpuTracks; }
	void SetAllCpuTracksToggle(bool bOnOff);
	void ShowAllCpuTracks() { SetAllCpuTracksToggle(true); }
	void HideAllCpuTracks() { SetAllCpuTracksToggle(false); }
	void ShowHideAllCpuTracks() { SetAllCpuTracksToggle(!IsAllCpuTracksToggleOn()); }

	TSharedPtr<const ITimingEvent> FindMaxEventInstance(uint32 TimerId, double StartTime, double EndTime);
	TSharedPtr<const ITimingEvent> FindMinEventInstance(uint32 TimerId, double StartTime, double EndTime);

private:
	void CreateThreadGroupsMenu(FMenuBuilder& MenuBuilder);

	bool ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const;
	void ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName);

private:
	STimingView* TimingView = nullptr;

	bool bShowHideAllGpuTracks = false;
	bool bShowHideAllCpuTracks = false;

	TSharedPtr<FGpuTimingTrack> GpuTrack;
	TSharedPtr<FGpuTimingTrack> Gpu2Track;

	/** Maps thread id to track pointer. */
	TMap<uint32, TSharedPtr<FCpuTimingTrack>> CpuTracks;

	/** Maps thread group name to thread group info. */
	TMap<const TCHAR*, FThreadGroup, FDefaultSetAllocator, TStringPointerMapKeyFuncs_DEPRECATED<const TCHAR*, FThreadGroup>> ThreadGroups;

	uint64 TimingProfilerTimelineCount = 0;
	uint64 LoadTimeProfilerTimelineCount = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace UE::Insights::TimingProfiler
