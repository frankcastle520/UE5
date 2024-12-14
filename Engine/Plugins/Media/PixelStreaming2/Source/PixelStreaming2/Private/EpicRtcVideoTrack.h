// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "epic_rtc/core/video/video_track.h"

namespace UE::PixelStreaming2
{
    class FEpicRtcVideoTrack
    {
    public:
        /**
	    * @return The id of the underlying EpicRtc data track.
	    */
	    EpicRtcStringView GetTrackId() const;

    protected:
        TRefCountPtr<EpicRtcVideoTrackInterface> Track;
    };
} // namespace UE::PixelStreaming2