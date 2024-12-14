// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcVideoCommon.h"
#include "Video/VideoConfig.h"

#include "epic_rtc/core/video/video_decoder.h"

namespace UE::PixelStreaming2
{

	class PIXELSTREAMING2_API FEpicRtcVideoDecoderInitializer : public EpicRtcVideoDecoderInitializerInterface, public TRefCountingMixin<FEpicRtcVideoDecoderInitializer>
	{
	public:
		FEpicRtcVideoDecoderInitializer() = default;
		virtual ~FEpicRtcVideoDecoderInitializer() = default;

		// Begin EpicRtcVideoDecoderInitializerInterface
		virtual void								 CreateDecoder(EpicRtcVideoCodecInfoInterface* CodecInfo, EpicRtcVideoDecoderInterface** OutDecoder) override;
		virtual EpicRtcStringView					 GetName() override;
		virtual EpicRtcVideoCodecInfoArrayInterface* GetSupportedCodecs() override;
		// End EpicRtcVideoDecoderInitializerInterface

	private:
		TMap<EVideoCodec, TArray<TRefCountPtr<EpicRtcVideoCodecInfoInterface>>> CreateSupportedDecoderMap();

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcVideoDecoderInitializer>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcVideoDecoderInitializer>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcVideoDecoderInitializer>::GetRefCount(); }
		// End EpicRtcRefCountInterface
	};

} // namespace UE::PixelStreaming2