// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/SlateRenderer.h"

/**
 * An "Video Consumer" is an object that is responsible for outputting the video received from a peer. For example, by
 * rendering to a render target.
 */
class PIXELSTREAMING2_API IPixelStreaming2VideoConsumer
{
public:
	virtual ~IPixelStreaming2VideoConsumer() = default;

	/**
	 * @brief Consume a texture as a video frame.
	 * @param Frame The Frame to consume.
	 */
	virtual void ConsumeFrame(FTextureRHIRef Frame) = 0;

	/**
	 * @brief Called when a video consumer is added.
	 */
	virtual void OnConsumerAdded() = 0;

	/**
	 * @brief Called when a video consumer is removed.
	 */
	virtual void OnConsumerRemoved() = 0;
};
