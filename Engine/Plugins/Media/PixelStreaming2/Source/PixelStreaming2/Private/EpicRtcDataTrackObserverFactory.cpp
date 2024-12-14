// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcDataTrackObserverFactory.h"

#include "EpicRtcDataTrackObserver.h"
#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcDataTrackObserverFactory::FEpicRtcDataTrackObserverFactory(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	EpicRtcErrorCode FEpicRtcDataTrackObserverFactory::CreateDataTrackObserver(const EpicRtcStringView ParticipantId, const EpicRtcStringView DataTrackId, EpicRtcDataTrackObserverInterface** OutDataTrackObserver)
	{
		EpicRtcDataTrackObserverInterface* DataTrackObserver = new FEpicRtcDataTrackObserver(Manager);
		// Because the ptr was created with new, we need to call AddRef ourself (ms spec compliant)
		DataTrackObserver->AddRef();

		*OutDataTrackObserver = DataTrackObserver;
		return EpicRtcErrorCode::Ok;
	}

} // namespace UE::PixelStreaming2
