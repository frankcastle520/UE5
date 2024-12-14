// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcAudioTrack.h"
	
namespace UE::PixelStreaming2
{
	EpicRtcStringView FEpicRtcAudioTrack::GetTrackId() const
	{
		if (!Track)
		{
			return EpicRtcStringView { ._ptr = nullptr, ._length = 0 };
		}

		return Track->GetId();
	}
} // namespace UE::PixelStreaming2