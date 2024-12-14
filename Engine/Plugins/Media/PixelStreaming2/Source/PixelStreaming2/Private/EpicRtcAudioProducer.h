// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Audio.h"
#include "ISubmixBufferListener.h"
#include "IPixelStreaming2AudioProducer.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcPatchInputProxy;

	/**
	 * An audio input capable of listening to UE submix's as well as receiving user audio via the PushAudio method.
	 * Any received audio will be passed into the Parent's PushAudio method
	 */
	class FEpicRtcAudioProducer : public ISubmixBufferListener, public IPixelStreaming2AudioProducer
	{
	public:
		static TSharedPtr<FEpicRtcAudioProducer> Create(Audio::FDeviceId AudioDeviceId, TSharedPtr<FEpicRtcPatchInputProxy> PatchInput);
		static TSharedPtr<FEpicRtcAudioProducer> Create(TSharedPtr<FEpicRtcPatchInputProxy> PatchInput);
		virtual ~FEpicRtcAudioProducer() = default;

		// For users to manually push non-submix audio into EpicRtc
		virtual void PushAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate) override;

		// ISubmixBufferListener interface
		virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;

	protected:
		FEpicRtcAudioProducer(TSharedPtr<FEpicRtcPatchInputProxy> PatchInput);

	private:
		TSharedPtr<FEpicRtcPatchInputProxy> PatchInput;
	};
} // namespace UE::PixelStreaming2