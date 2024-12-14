// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcDataTrackObserver.h"

#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcDataTrackObserver::FEpicRtcDataTrackObserver(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	void FEpicRtcDataTrackObserver::OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnDataTrackState.Broadcast(DataTrack, State);
	}

	void FEpicRtcDataTrackObserver::OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnDataTrackMessage.Broadcast(DataTrack);
	}

} // namespace UE::PixelStreaming2