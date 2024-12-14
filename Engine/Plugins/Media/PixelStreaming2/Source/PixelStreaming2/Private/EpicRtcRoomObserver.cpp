// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcRoomObserver.h"

#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcRoomObserver::FEpicRtcRoomObserver(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	void FEpicRtcRoomObserver::OnRoomStateUpdate(const EpicRtcRoomState State)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnRoomStateUpdate.Broadcast(State);
	}

	void FEpicRtcRoomObserver::OnRoomJoinedUpdate(EpicRtcParticipantInterface* Participant)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnRoomJoinedUpdate.Broadcast(Participant);
	}

	void FEpicRtcRoomObserver::OnRoomLeftUpdate(const EpicRtcStringView ParticipantId)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnRoomLeftUpdate.Broadcast(ParticipantId);
	}

	void FEpicRtcRoomObserver::OnAudioTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcAudioTrackInterface* AudioTrack)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnAudioTrackUpdate.Broadcast(Participant, AudioTrack);
	}

	void FEpicRtcRoomObserver::OnVideoTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcVideoTrackInterface* VideoTrack)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnVideoTrackUpdate.Broadcast(Participant, VideoTrack);
	}

	void FEpicRtcRoomObserver::OnDataTrackUpdate(EpicRtcParticipantInterface* Participant, EpicRtcDataTrackInterface* DataTrack)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnDataTrackUpdate.Broadcast(Participant, DataTrack);
	}

	[[nodiscard]] EpicRtcSdpInterface* FEpicRtcRoomObserver::OnLocalSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp)
	{
		if (!Manager.IsValid())
		{
			return nullptr;
		}

		Manager.Pin()->OnLocalSdpUpdate.Broadcast(Participant, Sdp);

		return nullptr;
	}

	[[nodiscard]] EpicRtcSdpInterface* FEpicRtcRoomObserver::OnRemoteSdpUpdate(EpicRtcParticipantInterface* Participant, EpicRtcSdpInterface* Sdp)
	{
		if (!Manager.IsValid())
		{
			return nullptr;
		}

		Manager.Pin()->OnRemoteSdpUpdate.Broadcast(Participant, Sdp);

		return nullptr;
	}

	void FEpicRtcRoomObserver::OnRoomErrorUpdate(const EpicRtcErrorCode Error)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnRoomErrorUpdate.Broadcast(Error);
	}

} // namespace UE::PixelStreaming2