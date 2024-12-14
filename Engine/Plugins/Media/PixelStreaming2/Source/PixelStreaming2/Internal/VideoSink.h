// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "IPixelStreaming2VideoSink.h"
#include "RendererInterface.h"

#include "epic_rtc/core/video/video_frame.h"

namespace UE::PixelStreaming2
{
	class PIXELSTREAMING2_API FVideoSink : public IPixelStreaming2VideoSink
	{
	public:
		virtual ~FVideoSink();

		virtual void AddVideoConsumer(IPixelStreaming2VideoConsumer* VideoConsumer) override;
		virtual void RemoveVideoConsumer(IPixelStreaming2VideoConsumer* VideoConsumer) override;

		bool HasVideoConsumers();

		void OnVideoData(const EpicRtcVideoFrame& Frame);

		void SetMuted(bool bIsMuted);

	protected:
		FCriticalSection					 VideoConsumersCS;
		TSet<IPixelStreaming2VideoConsumer*> VideoConsumers;

		bool bIsMuted = false;

		FCriticalSection				  RenderSyncContext;
		FPooledRenderTargetDesc			  RenderTargetDescriptor;
		TRefCountPtr<IPooledRenderTarget> RenderTarget;
		TArray<uint8_t>					  Buffer;
		FTextureRHIRef					  SourceTexture;

	private:
		void CallConsumeFrame(FTextureRHIRef Frame);
	};

} // namespace UE::PixelStreaming2