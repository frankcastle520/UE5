// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelCaptureCapturerMultiFormat.h"
#include "Video/Resources/VideoResourceRHI.h"

#include "epic_rtc/core/video/video_buffer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcVideoBufferMultiFormat;

	/**
	 * Base class for our multi format buffers.
	 */
	class FEpicRtcVideoBufferMultiFormatBase : public EpicRtcVideoBufferInterface, public TRefCountingMixin<FEpicRtcVideoBufferMultiFormatBase>
	{
	public:
		FEpicRtcVideoBufferMultiFormatBase(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer);
		virtual ~FEpicRtcVideoBufferMultiFormatBase() = default;

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
		// End EpicRtcVideoBufferInterface

	public:
		// Begin EpicRtcRefCountInterface
		virtual uint32_t AddRef() override final { return TRefCountingMixin<FEpicRtcVideoBufferMultiFormatBase>::AddRef(); }
		virtual uint32_t Release() override final { return TRefCountingMixin<FEpicRtcVideoBufferMultiFormatBase>::Release(); }
		virtual uint32_t Count() const override final { return TRefCountingMixin<FEpicRtcVideoBufferMultiFormatBase>::GetRefCount(); }
		// End EpicRtcRefCountInterface

	protected:
		TSharedPtr<FPixelCaptureCapturerMultiFormat> FrameCapturer;
	};

	/**
	 * A multi layered, multi format frame buffer for our encoder.
	 */
	class FEpicRtcVideoBufferMultiFormatLayered : public FEpicRtcVideoBufferMultiFormatBase
	{
	public:
		FEpicRtcVideoBufferMultiFormatLayered(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer);
		virtual ~FEpicRtcVideoBufferMultiFormatLayered() = default;

		virtual int GetWidth() override;
		virtual int GetHeight() override;

		int											 GetNumLayers() const;
		TRefCountPtr<FEpicRtcVideoBufferMultiFormat> GetLayer(int LayerIndex) const;
	};

	/**
	 * An empty frame, used for stream sharing to keep the encoder alive but do no encoding.
	 */
	class FEpicRtcVideoBufferEmpty : public FEpicRtcVideoBufferMultiFormatLayered
	{
	public:
		FEpicRtcVideoBufferEmpty(int InWidth, int InHeight)
			: FEpicRtcVideoBufferMultiFormatLayered(nullptr)
			, Width(InWidth)
			, Height(InHeight)
		{
		}

		virtual ~FEpicRtcVideoBufferEmpty() = default;
		virtual int GetWidth() override { return Width; };
		virtual int GetHeight() override { return Height; };

	private:
		int Width = 0;
		int Height = 0;
	};

	/**
	 * A single layer, multi format frame buffer.
	 */
	class FEpicRtcVideoBufferMultiFormat : public FEpicRtcVideoBufferMultiFormatBase
	{
	public:
		FEpicRtcVideoBufferMultiFormat(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer, int InLayerIndex);
		virtual ~FEpicRtcVideoBufferMultiFormat() = default;

		virtual int GetWidth() override;
		virtual int GetHeight() override;

		IPixelCaptureOutputFrame* RequestFormat(int32 Format) const;

	private:
		int32 LayerIndex;
		// we want the frame buffer to always refer to the same frame. so the first request for a format
		// will fill this cache.
		mutable TMap<int32, TSharedPtr<IPixelCaptureOutputFrame>> CachedFormat;
	};
} // namespace UE::PixelStreaming2