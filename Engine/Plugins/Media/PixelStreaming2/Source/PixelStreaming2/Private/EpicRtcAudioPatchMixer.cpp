// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcAudioPatchMixer.h"

namespace UE::PixelStreaming2
{
	FEpicRtcAudioPatchMixer::FEpicRtcAudioPatchMixer(uint8 NumChannels, uint32 SampleRate, float SampleSizeSeconds)
		: NumChannels(NumChannels)
		, SampleRate(SampleRate)
		, SampleSizeSeconds(SampleSizeSeconds)
	{
	}

	uint32 FEpicRtcAudioPatchMixer::GetMaxBufferSize() const
	{
		return NumChannels * SampleRate * SampleSizeSeconds;
	}

	uint8 FEpicRtcAudioPatchMixer::GetNumChannels() const
	{
		return NumChannels;
	}

	uint32 FEpicRtcAudioPatchMixer::GetSampleRate() const
	{
		return SampleRate;
	}
} // namespace UE::PixelStreaming2