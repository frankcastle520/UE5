// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcAudioTrackObserver.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/audio/audio_track_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcAudioTrackObserverFactory : public EpicRtcAudioTrackObserverFactoryInterface, public TRefCountingMixin<FEpicRtcAudioTrackObserverFactory>
	{
	public:
		FEpicRtcAudioTrackObserverFactory(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcAudioTrackObserverFactory() = default;

	public:
		// Begin EpicRtcAudioTrackObserverFactoryInterface 
		virtual EpicRtcErrorCode CreateAudioTrackObserver(const EpicRtcStringView ParticipantId, const EpicRtcStringView AudioTrackId, EpicRtcAudioTrackObserverInterface** OutAudioTrackObserver) override;
		// End EpicRtcAudioTrackObserverFactoryInterface 

	public:
		// Begin EpicRtcRefCountInterface 
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcAudioTrackObserverFactory>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcAudioTrackObserverFactory>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcAudioTrackObserverFactory>::GetRefCount(); }
		// End EpicRtcRefCountInterface 

	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};

} // namespace UE::PixelStreaming2