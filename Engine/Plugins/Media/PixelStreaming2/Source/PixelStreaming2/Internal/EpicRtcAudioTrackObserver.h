// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/audio/audio_track_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcAudioTrackObserver : public EpicRtcAudioTrackObserverInterface, public TRefCountingMixin<FEpicRtcAudioTrackObserver>
	{
	public:
		FEpicRtcAudioTrackObserver(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcAudioTrackObserver() = default;

	private:
		// Begin EpicRtcAudioTrackObserverInterface
		virtual void OnAudioTrackMuted(EpicRtcAudioTrackInterface* AudioTrack, EpicRtcBool bIsMuted) override;
		virtual void OnAudioTrackFrame(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcAudioFrame& Frame) override;
		virtual void OnAudioTrackRemoved(EpicRtcAudioTrackInterface* AudioTrack) override;
		virtual void OnAudioTrackState(EpicRtcAudioTrackInterface* AudioTrack, const EpicRtcTrackState State) override;
		// End EpicRtcAudioTrackObserverInterface

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcAudioTrackObserver>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcAudioTrackObserver>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcAudioTrackObserver>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};
} // namespace UE::PixelStreaming2