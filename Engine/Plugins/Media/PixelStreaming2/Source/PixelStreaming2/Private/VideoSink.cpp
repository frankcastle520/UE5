// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSink.h"

#include "Async/Async.h"
#include "ColorConversion.h"
#include "EpicRtcVideoBufferI420.h"
#include "EpicRtcVideoBufferRHI.h"
#include "IPixelStreaming2VideoConsumer.h"
#include "PixelStreaming2Trace.h"
#include "RenderTargetPool.h"

namespace UE::PixelStreaming2
{

	FVideoSink::~FVideoSink()
	{
		FScopeLock Lock(&VideoConsumersCS);
		for (auto Iter = VideoConsumers.CreateIterator(); Iter; ++Iter)
		{
			IPixelStreaming2VideoConsumer* VideoConsumer = Iter.ElementIt->Value;
			Iter.RemoveCurrent();
		}
	}

	void FVideoSink::AddVideoConsumer(IPixelStreaming2VideoConsumer* VideoConsumer)
	{
		FScopeLock Lock(&VideoConsumersCS);
		bool	   bAlreadyInSet = false;
		VideoConsumers.Add(VideoConsumer, &bAlreadyInSet);
		if (!bAlreadyInSet)
		{
			VideoConsumer->OnConsumerAdded();
		}
	}

	void FVideoSink::RemoveVideoConsumer(IPixelStreaming2VideoConsumer* VideoConsumer)
	{
		FScopeLock Lock(&VideoConsumersCS);
		if (VideoConsumers.Contains(VideoConsumer))
		{
			VideoConsumers.Remove(VideoConsumer);
			VideoConsumer->OnConsumerRemoved();
		}
	}

	bool FVideoSink::HasVideoConsumers()
	{
		return VideoConsumers.Num() > 0;
	}

	void FVideoSink::OnVideoData(const EpicRtcVideoFrame& Frame)
	{
		if (!HasVideoConsumers() || bIsMuted)
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FVideoSink::OnVideoData", PixelStreaming2Channel);

		int32_t Width = Frame._buffer->GetWidth();
		int32_t Height = Frame._buffer->GetHeight();

		if (Frame._buffer->GetFormat() == EpicRtcPixelFormat::Native)
		{
			UE::PixelStreaming2::FEpicRtcVideoBufferRHI* const FrameBuffer = static_cast<UE::PixelStreaming2::FEpicRtcVideoBufferRHI*>(Frame._buffer);
			if (FrameBuffer == nullptr)
			{
				return;
			}

			TSharedPtr<FVideoResourceRHI, ESPMode::ThreadSafe> VideoResource = FrameBuffer->GetVideoResource();
			if (VideoResource->GetFormat() != EVideoFormat::BGRA)
			{
				VideoResource = VideoResource->TransformResource(FVideoDescriptor(EVideoFormat::BGRA, Width, Height));
			}

			auto& Raw = StaticCastSharedPtr<FVideoResourceRHI>(VideoResource)->GetRaw();
			CallConsumeFrame(Raw.Texture);
		}
		else if (Frame._buffer->GetFormat() == EpicRtcPixelFormat::I420)
		{
			{
				FScopeLock	  Lock(&RenderSyncContext);
				const int32_t Size = Width * Height * 4;
				if (Size > Buffer.Num())
				{
					Buffer.SetNum(Size);
				}

				UE::PixelStreaming2::FEpicRtcVideoBufferI420* const FrameBuffer = static_cast<UE::PixelStreaming2::FEpicRtcVideoBufferI420*>(Frame._buffer);

				int StrideY = FrameBuffer->GetWidth();
				int StrideUV = (FrameBuffer->GetWidth() + 1) / 2;

				int DataSizeY = StrideY * FrameBuffer->GetHeight();
				int DataSizeUV = StrideUV * ((FrameBuffer->GetHeight() + 1) / 2);

				uint8_t* DataY = static_cast<uint8_t*>(FrameBuffer->GetData());
				uint8_t* DataU = static_cast<uint8_t*>(FrameBuffer->GetData()) + DataSizeY;
				uint8_t* DataV = static_cast<uint8_t*>(FrameBuffer->GetData()) + DataSizeY + DataSizeUV;

				UE::PixelStreaming2::ConvertI420ToArgb(
					DataY, StrideY,
					DataU, StrideUV,
					DataV, StrideUV,
					Buffer.GetData(), Width * 4,
					FrameBuffer->GetWidth(), FrameBuffer->GetHeight());
			}

			AsyncTask(ENamedThreads::GetRenderThread(), [Width, Height, this]() {
				FScopeLock Lock(&RenderSyncContext);

				const FIntPoint			  FrameSize = FIntPoint(Width, Height);
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

				if (!RenderTargetDescriptor.IsValid() || RenderTargetDescriptor.GetSize() != FIntVector(FrameSize.X, FrameSize.Y, 0))
				{
					// Create the RenderTarget descriptor
					RenderTargetDescriptor = FPooledRenderTargetDesc::Create2DDesc(FrameSize,
						PF_B8G8R8A8,
						FClearValueBinding::None,
						TexCreate_None,
						TexCreate_RenderTargetable,
						false);

					// Update the shader resource for the 'SourceTexture'
					FRHITextureCreateDesc RenderTargetTextureDesc =
						FRHITextureCreateDesc::Create2D(TEXT(""), FrameSize.X, FrameSize.Y, PF_B8G8R8A8)
							.SetClearValue(FClearValueBinding::None)
#if PLATFORM_MAC
							.SetFlags(ETextureCreateFlags::CPUReadback | ETextureCreateFlags::SRGB)
							.SetInitialState(ERHIAccess::CPURead);
#else
							.SetFlags(ETextureCreateFlags::Dynamic | ETextureCreateFlags::ShaderResource | TexCreate_RenderTargetable | ETextureCreateFlags::SRGB)
							.SetInitialState(ERHIAccess::SRVMask);
#endif

					SourceTexture = RHICreateTexture(RenderTargetTextureDesc);

					// Find a free target-able texture from the render pool
					GRenderTargetPool.FindFreeElement(RHICmdList,
						RenderTargetDescriptor,
						RenderTarget,
						TEXT("PIXELSTEAMINGPLAYER"));
				}

				// Create the update region structure
				const FUpdateTextureRegion2D Region(0, 0, 0, 0, FrameSize.X, FrameSize.Y);

				// Set the Pixel data of the webrtc Frame to the SourceTexture
				RHIUpdateTexture2D(SourceTexture, 0, Region, FrameSize.X * 4, Buffer.GetData());

				CallConsumeFrame(SourceTexture);
			});
		}
	}

	void FVideoSink::CallConsumeFrame(FTextureRHIRef Frame)
	{
		// Iterate video consumers and pass this data to their buffers
		FScopeLock Lock(&VideoConsumersCS);
		for (IPixelStreaming2VideoConsumer* VideoConsumer : VideoConsumers)
		{
			VideoConsumer->ConsumeFrame(Frame);
		}
	}

	void FVideoSink::SetMuted(bool bInIsMuted)
	{
		bIsMuted = bInIsMuted;
	}

} // namespace UE::PixelStreaming2