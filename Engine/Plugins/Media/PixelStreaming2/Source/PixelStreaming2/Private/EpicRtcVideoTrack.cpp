// Copyright Epic Games, Inc. All Rights Reserved.

#include "EpicRtcVideoTrack.h"

namespace UE::PixelStreaming2
{
    EpicRtcStringView FEpicRtcVideoTrack::GetTrackId() const
    {
        if (!Track)
		{
			return EpicRtcStringView { ._ptr = nullptr, ._length = 0 };
		}

		return Track->GetId();
    }
}
