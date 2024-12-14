// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcAudioTrackObserverFactory.h"

#include "EpicRtcAudioTrackObserver.h"
#include "EpicRtcManager.h"

namespace UE::PixelStreaming2
{

	FEpicRtcAudioTrackObserverFactory::FEpicRtcAudioTrackObserverFactory(TWeakPtr<FEpicRtcManager> Manager)
		: Manager(Manager)
	{
	}

	EpicRtcErrorCode FEpicRtcAudioTrackObserverFactory::CreateAudioTrackObserver(const EpicRtcStringView ParticipantId, const EpicRtcStringView AudioTrackId, EpicRtcAudioTrackObserverInterface** OutAudioTrackObserver)
	{
		EpicRtcAudioTrackObserverInterface* AudioTrackObserver = new FEpicRtcAudioTrackObserver(Manager);
		// Because the ptr was created with new, we need to call AddRef ourself (ms spec compliant)
		AudioTrackObserver->AddRef();

		*OutAudioTrackObserver = AudioTrackObserver;
		return EpicRtcErrorCode::Ok;
	}

} // namespace UE::PixelStreaming2