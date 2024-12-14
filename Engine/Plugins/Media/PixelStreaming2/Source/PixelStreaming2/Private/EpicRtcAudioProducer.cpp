// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcAudioProducer.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "EpicRtcAudioMixingCapturer.h"

namespace UE::PixelStreaming2
{
	TSharedPtr<FEpicRtcAudioProducer> FEpicRtcAudioProducer::Create(Audio::FDeviceId InAudioDeviceId, TSharedPtr<FEpicRtcPatchInputProxy> InPatchInput)
	{
		TSharedPtr<FEpicRtcAudioProducer> Listener = TSharedPtr<FEpicRtcAudioProducer>(new FEpicRtcAudioProducer(InPatchInput));
		if (FAudioDevice* AudioDevice = FAudioDeviceManager::Get()->GetAudioDeviceRaw(InAudioDeviceId))
		{
			AudioDevice->RegisterSubmixBufferListener(Listener.ToSharedRef(), AudioDevice->GetMainSubmixObject());

			// RegisterSubmixBufferListener lazily enqueues the registration on the audio thread,
			// so we have to wait for the audio thread to destroy.
			FAudioCommandFence Fence;
			Fence.BeginFence();
			Fence.Wait();
		}

		return Listener;
	}

	TSharedPtr<FEpicRtcAudioProducer> FEpicRtcAudioProducer::Create(TSharedPtr<FEpicRtcPatchInputProxy> InPatchInput)
	{
		TSharedPtr<FEpicRtcAudioProducer> Listener = TSharedPtr<FEpicRtcAudioProducer>(new FEpicRtcAudioProducer(InPatchInput));
		return Listener;
	}

	FEpicRtcAudioProducer::FEpicRtcAudioProducer(TSharedPtr<FEpicRtcPatchInputProxy> PatchInput)
		: PatchInput(PatchInput)
	{
	}

	void FEpicRtcAudioProducer::PushAudio(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate)
	{
		PatchInput->PushAudio(AudioData, NumSamples, NumChannels, SampleRate);
	}

	void FEpicRtcAudioProducer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
	{
		PatchInput->PushAudio(AudioData, NumSamples, NumChannels, SampleRate);
	}
} // namespace UE::PixelStreaming2