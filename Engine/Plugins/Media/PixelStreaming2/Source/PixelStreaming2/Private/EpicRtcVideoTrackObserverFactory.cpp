// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcVideoTrackObserverFactory.h"

#include "EpicRtcManager.h"
#include "EpicRtcVideoTrackObserver.h"

namespace UE::PixelStreaming2
{

	FEpicRtcVideoTrackObserverFactory::FEpicRtcVideoTrackObserverFactory(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	EpicRtcErrorCode FEpicRtcVideoTrackObserverFactory::CreateVideoTrackObserver(const EpicRtcStringView ParticipantId, const EpicRtcStringView VideoTrackId, EpicRtcVideoTrackObserverInterface** OutVideoTrackObserver)
	{
		EpicRtcVideoTrackObserverInterface* VideoTrackObserver = new FEpicRtcVideoTrackObserver(Manager);
		// Because the ptr was created with new, we need to call AddRef ourself (ms spec compliant)
		VideoTrackObserver->AddRef();

		*OutVideoTrackObserver = VideoTrackObserver;
		return EpicRtcErrorCode::Ok;
	}

} // namespace UE::PixelStreaming2