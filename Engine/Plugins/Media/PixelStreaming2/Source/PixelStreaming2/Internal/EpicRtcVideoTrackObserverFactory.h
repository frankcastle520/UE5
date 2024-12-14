// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/video/video_track_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcVideoTrackObserverFactory : public EpicRtcVideoTrackObserverFactoryInterface, public TRefCountingMixin<FEpicRtcVideoTrackObserverFactory>
	{
	public:
		FEpicRtcVideoTrackObserverFactory(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcVideoTrackObserverFactory() = default;

	public:
		// Begin EpicRtcVideoTrackObserverFactoryInterface
		virtual EpicRtcErrorCode CreateVideoTrackObserver(const EpicRtcStringView ParticipantId, const EpicRtcStringView VideoTrackId, EpicRtcVideoTrackObserverInterface** OutVideoTrackObserver) override;
		// End EpicRtcVideoTrackObserverFactoryInterface

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcVideoTrackObserverFactory>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcVideoTrackObserverFactory>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcVideoTrackObserverFactory>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};

} // namespace UE::PixelStreaming2