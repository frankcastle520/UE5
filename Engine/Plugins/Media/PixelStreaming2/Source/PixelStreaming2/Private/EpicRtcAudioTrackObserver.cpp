// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcAudioTrackObserver.h"

#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcAudioTrackObserver::FEpicRtcAudioTrackObserver(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	void FEpicRtcAudioTrackObserver::OnAudioTrackMuted(EpicRtcAudioTrackInterface* AudioTrack, EpicRtcBool bIsMuted)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnAudioTrackMuted.Broadcast(AudioTrack, bIsMuted);
	}

	void FEpicRtcAudioTrackObserver::OnAudioTrackFrame(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcAudioFrame& Frame)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnAudioTrackFrame.Broadcast(AudioTrack, Frame);
	}

	void FEpicRtcAudioTrackObserver::OnAudioTrackRemoved(EpicRtcAudioTrackInterface* AudioTrack)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnAudioTrackRemoved.Broadcast(AudioTrack);
	}

	void FEpicRtcAudioTrackObserver::OnAudioTrackState(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcTrackState State)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnAudioTrackState.Broadcast(AudioTrack, State);
	}

} // namespace UE::PixelStreaming2