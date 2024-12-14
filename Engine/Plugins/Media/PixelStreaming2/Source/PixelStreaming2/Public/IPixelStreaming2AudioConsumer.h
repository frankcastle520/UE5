// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * An "Audio Consumer" is an object that is responsible for outputting the audio received from a peer. For example, by
 * passing the audio into a UE submix.
 */
class PIXELSTREAMING2_API IPixelStreaming2AudioConsumer
{
public:
	virtual ~IPixelStreaming2AudioConsumer() = default;

	/**
	 * @brief Consume raw audio data.
	 * @param AudioData Pointer to the audio data.
	 * @param InSampleRate Audio sample rate in samples per second.
	 * @param NChannels Number of audio channels. For example 2 for stero audio.
	 * @param NFrames Number of Audio frames in a single channel.
	 */
	virtual void ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames) = 0;

	/**
	 * @brief Called when a audio consumer is added.
	 */
	virtual void OnConsumerAdded() = 0;

	/**
	 * @brief Called when a audio consumer is removed.
	 */
	virtual void OnConsumerRemoved() = 0;
};
