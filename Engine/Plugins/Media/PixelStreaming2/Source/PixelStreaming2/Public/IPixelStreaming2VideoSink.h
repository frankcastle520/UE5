// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IPixelStreaming2VideoConsumer;

/**
 * Interface for a sink that collects video coming in from the browser and passes into into UE.
 */
class PIXELSTREAMING2_API IPixelStreaming2VideoSink
{
public:
	virtual ~IPixelStreaming2VideoSink() = default;

	/**
	 * @brief Add a video consumer to the sink.
	 * @param VideoConsumer The video consumer to add to the sink.
	 */
	virtual void AddVideoConsumer(IPixelStreaming2VideoConsumer* VideoConsumer) = 0;

	/**
	 * @brief Remove a video consumer to remove from the sink.
	 * @param VideoConsumer The video consumer to remove from the sink.
	 */
	virtual void RemoveVideoConsumer(IPixelStreaming2VideoConsumer* VideoConsumer) = 0;
};
