// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcVideoTrackObserver.h"

#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcVideoTrackObserver::FEpicRtcVideoTrackObserver(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	void FEpicRtcVideoTrackObserver::OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnVideoTrackMuted.Broadcast(VideoTrack, bIsMuted);
	}

	void FEpicRtcVideoTrackObserver::OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnVideoTrackFrame.Broadcast(VideoTrack, Frame);
	}

	void FEpicRtcVideoTrackObserver::OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnVideoTrackRemoved.Broadcast(VideoTrack);
	}

	void FEpicRtcVideoTrackObserver::OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnVideoTrackState.Broadcast(VideoTrack, State);
	}

} // namespace UE::PixelStreaming2