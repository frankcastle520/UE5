// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RTCStatsCollector.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/stats.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcStatsCollector : public EpicRtcStatsCollectorCallbackInterface, public TRefCountingMixin<FEpicRtcStatsCollector>
	{
	public:
		FEpicRtcStatsCollector() = default;
		~FEpicRtcStatsCollector() = default;

		// Begin EpicRtcStatsCollectorCallbackInterface interface
		void			 OnStatsDelivered(const EpicRtcStatsReport& InReport) override;
		// End EpicRtcStatsCollectorCallbackInterface interface

		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcStatsCollector>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcStatsCollector>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcStatsCollector>::GetRefCount(); }
		// End EpicRtcStatsCollectorCallbackInterface interface

		DECLARE_EVENT_TwoParams(FEpicRtcStatsCollector, FOnStatsReady, const FString&, const EpicRtcConnectionStats&);
		FOnStatsReady OnStatsReady;
	};
} // namespace UE::PixelStreaming2