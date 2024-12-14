// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcVideoBufferMultiFormat.h"

namespace UE::PixelStreaming2
{
    FEpicRtcVideoBufferMultiFormatBase::FEpicRtcVideoBufferMultiFormatBase(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer)
		: FrameCapturer(InFrameCapturer)
	{
	}

	FEpicRtcVideoBufferMultiFormatLayered::FEpicRtcVideoBufferMultiFormatLayered(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer)
		: FEpicRtcVideoBufferMultiFormatBase(InFrameCapturer)
	{
	}

	int FEpicRtcVideoBufferMultiFormatLayered::GetWidth()
	{
		return FrameCapturer ? FrameCapturer->GetWidth(GetNumLayers() - 1) : -1;
	}

	int FEpicRtcVideoBufferMultiFormatLayered::GetHeight()
	{
		return FrameCapturer ? FrameCapturer->GetHeight(GetNumLayers() - 1) : -1;
	}

	int FEpicRtcVideoBufferMultiFormatLayered::GetNumLayers() const
	{
		return FrameCapturer ? FrameCapturer->GetNumLayers() : -1;
	}

	TRefCountPtr<FEpicRtcVideoBufferMultiFormat> FEpicRtcVideoBufferMultiFormatLayered::GetLayer(int LayerIndex) const
	{
		return new FEpicRtcVideoBufferMultiFormat(FrameCapturer, LayerIndex);
	}

	FEpicRtcVideoBufferMultiFormat::FEpicRtcVideoBufferMultiFormat(TSharedPtr<FPixelCaptureCapturerMultiFormat> InFrameCapturer, int InLayerIndex)
		: FEpicRtcVideoBufferMultiFormatBase(InFrameCapturer)
		, LayerIndex(InLayerIndex)
	{
	}

	int FEpicRtcVideoBufferMultiFormat::GetWidth()
	{
		return FrameCapturer ? FrameCapturer->GetWidth(LayerIndex) : -1;
	}

	int FEpicRtcVideoBufferMultiFormat::GetHeight()
	{
		return FrameCapturer ? FrameCapturer->GetHeight(LayerIndex) : -1;
	}

	IPixelCaptureOutputFrame* FEpicRtcVideoBufferMultiFormat::RequestFormat(int32 Format) const
	{
		// ensure this frame buffer will always refer to the same frame
		if (TSharedPtr<IPixelCaptureOutputFrame>* CachedFrame = CachedFormat.Find(Format))
		{
			return CachedFrame->Get();
		}

		if(!FrameCapturer)
		{
			return nullptr;
		}

		TSharedPtr<IPixelCaptureOutputFrame> Frame = FrameCapturer->WaitForFormat(Format, LayerIndex);
		CachedFormat.Add(Format, Frame);
		return Frame.Get();
	}
}