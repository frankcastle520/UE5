// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/data_track_observer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcManager;

	class PIXELSTREAMING2_API FEpicRtcDataTrackObserver : public EpicRtcDataTrackObserverInterface, public TRefCountingMixin<FEpicRtcDataTrackObserver>
	{
	public:
		FEpicRtcDataTrackObserver(TWeakPtr<FEpicRtcManager> Manager);
		virtual ~FEpicRtcDataTrackObserver() = default;

	private:
		// Begin EpicRtcDataTrackObserverInterface
		virtual void OnDataTrackState(EpicRtcDataTrackInterface* DataTrack, const EpicRtcTrackState State) override;
		virtual void OnDataTrackMessage(EpicRtcDataTrackInterface* DataTrack) override;
		virtual void OnDataTrackError(EpicRtcDataTrackInterface*, const EpicRtcErrorCode) override {};
		// End EpicRtcDataTrackObserverInterface

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcDataTrackObserver>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcDataTrackObserver>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcDataTrackObserver>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TWeakPtr<FEpicRtcManager> Manager;
	};

} // namespace UE::PixelStreaming2