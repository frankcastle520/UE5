// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/video/video_track_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcVideoTrackObserver : public EpicRtcVideoTrackObserverInterface, public TRefCountingMixin<FEpicRtcVideoTrackObserver>
	{
	public:
		FEpicRtcVideoTrackObserver(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcVideoTrackObserver() = default;

	private:
		// Begin EpicRtcVideoTrackObserverInterface
		virtual void		OnVideoTrackMuted(EpicRtcVideoTrackInterface* VideoTrack, EpicRtcBool bIsMuted) override;
		virtual void		OnVideoTrackFrame(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcVideoFrame& Frame) override;
		virtual void		OnVideoTrackRemoved(EpicRtcVideoTrackInterface* VideoTrack) override;
		virtual void		OnVideoTrackState(EpicRtcVideoTrackInterface* VideoTrack, const EpicRtcTrackState State) override;
		virtual void		OnVideoTrackEncodedFrame(EpicRtcVideoTrackInterface*, const EpicRtcEncodedVideoFrame&) override {};
		virtual EpicRtcBool Enabled() const override { return true; };
		// End EpicRtcVideoTrackObserverInterface

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcVideoTrackObserver>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcVideoTrackObserver>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcVideoTrackObserver>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};

} // namespace UE::PixelStreaming2