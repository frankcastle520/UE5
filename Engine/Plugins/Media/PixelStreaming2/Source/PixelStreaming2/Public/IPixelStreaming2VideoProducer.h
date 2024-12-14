// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IPixelCaptureInputFrame.h"

/**
 * A "Video Producer" is an object that you use to push video frames into the Pixel Streaming system.
 *
 * Example usage:
 *
 * (1) Each new frame you want to push you call `MyVideoProducer->PushFrame(MyFrame)`
 *
 * Note: Your frame is likely a `FPixelCaptureInputFrameRHI` if the frame is a GPU texture or
 * a `FPixelCaptureInputFrameI420` or `FPixelCaptureInputFrameNV12` if your frame is from the CPU.
 */
class PIXELSTREAMING2_API IPixelStreaming2VideoProducer
{
public:
    IPixelStreaming2VideoProducer() = default;
	virtual ~IPixelStreaming2VideoProducer() = default;

	/**
	 * Pushes a raw video frame into the Pixel Streaming system.
	 * @param InputFrame The raw input frame, which may be a GPU texture or a CPU texture based on the underlying type of frame pushed.
	 */
	virtual void PushFrame(const IPixelCaptureInputFrame& InputFrame) = 0;

    /**
	 * A human readable identifier used when displaying what the streamer is streaming in the toolbar
	 * @return A string containing the display name.
	 */
	virtual FString ToString() = 0;
};