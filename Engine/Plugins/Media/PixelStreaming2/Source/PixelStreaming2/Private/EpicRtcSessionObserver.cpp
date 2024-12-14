// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcSessionObserver.h"

#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcSessionObserver::FEpicRtcSessionObserver(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	void FEpicRtcSessionObserver::OnSessionStateUpdate(const EpicRtcSessionState State)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnSessionStateUpdate.Broadcast(State);
	}

	void FEpicRtcSessionObserver::OnSessionErrorUpdate(const EpicRtcErrorCode Error)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnSessionErrorUpdate.Broadcast(Error);
	}

	void FEpicRtcSessionObserver::OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* RoomsList)
	{
		if (!Manager.IsValid())
		{
			return;
		}

		Manager.Pin()->OnSessionRoomsAvailableUpdate.Broadcast(RoomsList);
	}

} // namespace UE::PixelStreaming2
