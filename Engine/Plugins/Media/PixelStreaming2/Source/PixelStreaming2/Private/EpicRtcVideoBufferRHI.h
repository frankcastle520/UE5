// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Video/Resources/VideoResourceRHI.h"

#include "epic_rtc/core/video/video_buffer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcVideoBufferRHI : public EpicRtcVideoBufferInterface, public TRefCountingMixin<FEpicRtcVideoBufferRHI>
	{
	public:
		FEpicRtcVideoBufferRHI(TSharedPtr<FVideoResourceRHI> VideoResourceRHI)
			: VideoResourceRHI(VideoResourceRHI) 
		{
			VideoResourceRHI->SetUsing(true);
		}

		virtual ~FEpicRtcVideoBufferRHI() 
		{
			VideoResourceRHI->SetUsing(false);
		}

	public:
		// Begin EpicRtcVideoBufferInterface
		virtual void* GetData() override
		{
			unimplemented();
			return nullptr;
		}

		virtual EpicRtcPixelFormat GetFormat() override
		{
			return EpicRtcPixelFormat::Native;
		}

		virtual int GetWidth() override
		{
			return VideoResourceRHI->GetDescriptor().Width;
		}

		virtual int GetHeight() override
		{
			return VideoResourceRHI->GetDescriptor().Height;
		}
		// End EpicRtcVideoBufferInterface

		TSharedPtr<FVideoResourceRHI> GetVideoResource() { return VideoResourceRHI; }

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcVideoBufferRHI>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcVideoBufferRHI>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcVideoBufferRHI>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TSharedPtr<FVideoResourceRHI> VideoResourceRHI;
	};
} // namespace UE::PixelStreaming2