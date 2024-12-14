// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DSP/MultithreadedPatching.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcAudioPatchMixer : public Audio::FPatchMixer
	{
	public:
		FEpicRtcAudioPatchMixer(uint8 NumChannels, uint32 SampleRate, float SampleSizeSeconds);

		uint32 GetMaxBufferSize() const;
		uint8  GetNumChannels() const;
		uint32 GetSampleRate() const;

	private:
		uint8  NumChannels;
		uint32 SampleRate;
		float  SampleSizeSeconds;
	};
} // namespace UE::PixelStreaming2