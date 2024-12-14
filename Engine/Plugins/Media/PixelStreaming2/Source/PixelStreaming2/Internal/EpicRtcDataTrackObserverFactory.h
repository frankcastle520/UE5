// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcDataTrackObserver.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/data_track_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcDataTrackObserverFactory : public EpicRtcDataTrackObserverFactoryInterface, public TRefCountingMixin<FEpicRtcDataTrackObserverFactory>
	{
	public:
		FEpicRtcDataTrackObserverFactory(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcDataTrackObserverFactory() = default;

	public:
		// Begin EpicRtcDataTrackObserverFactoryInterface 
		virtual EpicRtcErrorCode CreateDataTrackObserver(const EpicRtcStringView ParticipantId, const EpicRtcStringView DataTrackId, EpicRtcDataTrackObserverInterface** OutDataTrackObserver) override;
		// End EpicRtcDataTrackObserverFactoryInterface 

	public:
		// Begin EpicRtcRefCountInterface 
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcDataTrackObserverFactory>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcDataTrackObserverFactory>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcDataTrackObserverFactory>::GetRefCount(); }
		// End EpicRtcRefCountInterface 

	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};

} // namespace UE::PixelStreaming2