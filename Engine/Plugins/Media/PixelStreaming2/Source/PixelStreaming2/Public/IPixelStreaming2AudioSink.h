// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IPixelStreaming2AudioConsumer;

/**
 * An "Audio Sink" is an object that receives audio from a singular peer. From here, you can add multiple consumers to
 * output the received audio.
 */
class PIXELSTREAMING2_API IPixelStreaming2AudioSink
{
public:
	virtual ~IPixelStreaming2AudioSink() = default;

	/**
	 * @brief Add an audio consumer to the sink.
	 * @param AudioConsumer The Audio consumer to add to the sink.
	 */
	virtual void AddAudioConsumer(IPixelStreaming2AudioConsumer* AudioConsumer) = 0;

	/**
	 * @brief Remove an audio consumer to remove from the sink.
	 * @param AudioConsumer The Audio consumer to remove from the sink.
	 */
	virtual void RemoveAudioConsumer(IPixelStreaming2AudioConsumer* AudioConsumer) = 0;		
};