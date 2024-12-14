// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreaming2Stats.h"
#include "Video/Decoders/Configs/VideoDecoderConfigAV1.h"
#include "Video/Decoders/Configs/VideoDecoderConfigH264.h"
#include "Video/Decoders/Configs/VideoDecoderConfigVP8.h"
#include "Video/Decoders/Configs/VideoDecoderConfigVP9.h"
#include "Video/Resources/VideoResourceCPU.h"
#include "Video/Resources/VideoResourceRHI.h"
#include "Video/VideoDecoder.h"

#include "epic_rtc/core/video/video_decoder.h"

#include <atomic>
#include <type_traits>

namespace UE::PixelStreaming2
{
	template <typename TResolvableVideoResource, typename TVideoResource>
	class TVideoResourcePool
	{
	public:
		TResolvableVideoResource& GetOrCreate()
		{
			ON_SCOPE_EXIT
			{
				IPixelStreaming2Stats::Get().GraphValue(TEXT("NumDecodeResource"), Resources.Num(), 1, 0.f, 120.f);
			};

			if (RHIGetInterfaceType() == ERHIInterfaceType::Vulkan)
			{
				// Vulkan has lifetime issues so we always use the same resource
				if (Resources.Num() == 0)
				{
					return Resources.Emplace_GetRef();
				}
				return Resources[0];
			}

			TResolvableVideoResource* Resource = Resources.FindByPredicate([](TResolvableVideoResource& ResolvableResource) {
				TSharedPtr<TVideoResource>& Resolved = ResolvableResource; // Activates the TSharedPtr operator overload which will resolve the resource
				if (!Resolved.IsValid())
				{
					return false;
				}

				return !Resolved->IsInUse();
			});

			if (!Resource)
			{
				return Resources.Emplace_GetRef();
			}

			return *Resource;
		}

	private:
		TArray<TResolvableVideoResource> Resources;
	};

	template <std::derived_from<FVideoResource> TVideoResource>
	class TEpicRtcVideoDecoder : public EpicRtcVideoDecoderInterface, public TRefCountingMixin<TEpicRtcVideoDecoder<TVideoResource>>
	{
	public:
		TEpicRtcVideoDecoder(EpicRtcVideoCodecInfoInterface* CodecInfo);

		// Begin EpicRtcVideoDecoderInterface
		[[nodiscard]] virtual EpicRtcStringView GetName() const override;
		virtual EpicRtcVideoDecoderConfig		GetConfig() const override;
		virtual EpicRtcMediaResult				SetConfig(const EpicRtcVideoDecoderConfig& VideoDecoderConfig) override;
		virtual EpicRtcMediaResult				Decode(const EpicRtcEncodedVideoFrame& Frame) override;
		virtual void							RegisterCallback(EpicRtcVideoDecoderCallbackInterface* Callback) override;
		virtual void							Reset() override;
		// End EpicRtcVideoDecoderInterface
	private:
		TSharedPtr<TVideoDecoder<TVideoResource>>		   Decoder;
		TUniquePtr<FVideoDecoderConfig>					   InitialVideoConfig;
		EpicRtcVideoDecoderConfig						   DecoderConfig;
		TRefCountPtr<EpicRtcVideoDecoderCallbackInterface> VideoDecoderCallback;
		TRefCountPtr<EpicRtcVideoCodecInfoInterface>	   CodecInfo;
		uint16_t										   FrameCount;

		// clang-format off
		using ResourcePoolType = typename std::conditional<std::is_same_v<TVideoResource, FVideoResourceRHI>, TVideoResourcePool<FResolvableVideoResourceRHI, FVideoResourceRHI>, 
								 typename std::conditional<std::is_same_v<TVideoResource, FVideoResourceCPU>, TVideoResourcePool<FResolvableVideoResourceCPU, FVideoResourceCPU>, 
								 // We only support RHI and CPU resources so if we create one that isn't this type it will error (on purpose)
								 void>::type>::type; 

		ResourcePoolType VideoResources;
		// clang-format on

	private:
		bool LateInitDecoder();

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<TEpicRtcVideoDecoder>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<TEpicRtcVideoDecoder>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<TEpicRtcVideoDecoder>::GetRefCount(); }
		// End EpicRtcRefCountInterface
	};
} // namespace UE::PixelStreaming2