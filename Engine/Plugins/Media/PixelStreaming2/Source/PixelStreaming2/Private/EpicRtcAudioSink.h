// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioSink.h"
#include "EpicRtcAudioTrack.h"

namespace UE::PixelStreaming2
{
	// Collects audio coming in from EpicRtc and passes into into UE's audio system.
	class FEpicRtcAudioSink : public FAudioSink, public FEpicRtcAudioTrack
	{
	public:
		static TSharedPtr<FEpicRtcAudioSink> Create(TRefCountPtr<EpicRtcAudioTrackInterface> InTrack);
		virtual ~FEpicRtcAudioSink() = default;

	private:
		FEpicRtcAudioSink(TRefCountPtr<EpicRtcAudioTrackInterface> InTrack);
	};
} // namespace UE::PixelStreaming2