// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EpicRtcAudioTrack.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/audio/audio_track.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcAudioSource : public FEpicRtcAudioTrack
	{
	public:
		static TSharedPtr<FEpicRtcAudioSource> Create(TRefCountPtr<EpicRtcAudioTrackInterface> InTrack);

		void OnAudioBuffer(int16_t* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate);
		void SetMuted(bool bIsMuted);

	private:
		FEpicRtcAudioSource(TRefCountPtr<EpicRtcAudioTrackInterface> InTrack);

		bool bIsMuted;
	};
} // namespace UE::PixelStreaming2