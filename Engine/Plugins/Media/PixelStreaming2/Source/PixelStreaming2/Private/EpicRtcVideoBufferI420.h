// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelCaptureBufferI420.h"

#include "epic_rtc/core/video/video_buffer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcVideoBufferI420 : public EpicRtcVideoBufferInterface, public TRefCountingMixin<FEpicRtcVideoBufferI420>
	{
	public:
		FEpicRtcVideoBufferI420(TSharedPtr<FPixelCaptureBufferI420> Buffer)
			: Buffer(Buffer) {}
		virtual ~FEpicRtcVideoBufferI420() = default;

	public:
		// Begin EpicRtcVideoBufferInterface
		virtual void* GetData() override
		{
			return Buffer->GetMutableData();
		}

		virtual EpicRtcPixelFormat GetFormat() override
		{
			return EpicRtcPixelFormat::I420;
		}

		virtual int GetWidth() override
		{
			return Buffer->GetWidth();
		}

		virtual int GetHeight() override
		{
			return Buffer->GetHeight();
		}
		// End EpicRtcVideoBufferInterface

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcVideoBufferI420>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcVideoBufferI420>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcVideoBufferI420>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	private:
		TSharedPtr<FPixelCaptureBufferI420> Buffer;
	};
} // namespace UE::PixelStreaming2