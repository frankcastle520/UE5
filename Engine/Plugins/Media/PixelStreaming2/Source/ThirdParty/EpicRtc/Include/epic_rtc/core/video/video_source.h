// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/containers/epic_rtc_span.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/video/video_encoding_config.h"

#pragma pack(push, 8)

/**
 * Describes video source that can be added to EpicRtcConnectionInterface
 */
struct EpicRtcVideoSource
{
    /**
     * Id that is set on the video stream.
     * If set to null, a default value is used.
     * This is used to sync the audio and video stream.
     */
    EpicRtcStringView _streamId;

    /**
     * Video source encodings, use more than one to get simulcast functionality.
     */
    EpicRtcVideoEncodingConfigSpan _encodings;

    /**
     * Indicates source direction.
     */
    EpicRtcMediaSourceDirection _direction;
};

static_assert(sizeof(EpicRtcVideoSource) == 40);  // Ensure EpicRtcVideoSource is expected size on all platforms

#pragma pack(pop)
