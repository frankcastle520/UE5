// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"

#include "epic_rtc/core/audio/audio_track.h"

namespace UE::PixelStreaming2
{
	class FEpicRtcAudioTrack
	{
	public:
		/**
		 * @return The id of the underlying EpicRtc data track.
		 */
		EpicRtcStringView GetTrackId() const;

	protected:
		TRefCountPtr<EpicRtcAudioTrackInterface> Track;
	};
} // namespace UE::PixelStreaming2