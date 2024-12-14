// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcAudioSource.h"

#include "EpicRtcAudioMixingCapturer.h"
#include "PixelStreaming2Module.h"

namespace UE::PixelStreaming2
{

	TSharedPtr<FEpicRtcAudioSource> FEpicRtcAudioSource::Create(TRefCountPtr<EpicRtcAudioTrackInterface> InTrack)
	{
		TSharedPtr<FEpicRtcAudioSource> AudioTrack = TSharedPtr<FEpicRtcAudioSource>(new FEpicRtcAudioSource(InTrack));

		TSharedPtr<FEpicRtcAudioMixingCapturer> Capturer = FPixelStreaming2Module::GetModule()->GetAudioCapturer();
		Capturer->OnAudioBuffer.AddSP(AudioTrack.ToSharedRef(), &FEpicRtcAudioSource::OnAudioBuffer);

		return AudioTrack;
	}

	FEpicRtcAudioSource::FEpicRtcAudioSource(TRefCountPtr<EpicRtcAudioTrackInterface> InTrack)
		: bIsMuted(false)
	{
		Track = InTrack;
	}

	void FEpicRtcAudioSource::SetMuted(bool bInIsMuted)
	{
		bIsMuted = bInIsMuted;
	}

	void FEpicRtcAudioSource::OnAudioBuffer(int16_t* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate)
	{
		if (!Track || bIsMuted)
		{
			return;
		}

		const uint32_t NumFrames = NumSamples / NumChannels;
		EpicRtcAudioFrame AudioFrame
		{
			._data = AudioData,
			._length = NumFrames,
			._timestamp = 0,
			._format =
			{
				._sampleRate = (uint32_t)SampleRate,
				._numChannels = (uint32_t)NumChannels,
				._parameters = nullptr
			}
		};

		// Because UE handles all audio processing, we can bypass the ADM.
		// This also has the added benefit of increasing audio quality
		Track->PushFrame(AudioFrame, true);
	}

} // namespace UE::PixelStreaming2

