// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"

#include <cstdint>

#pragma pack(push, 8)

/**
 * Describes audio source that can be added to EpicRtcConnectionInterface
 */
struct EpicRtcAudioSource
{
    /**
     * Id that is set on the audio stream.
     * If set to null, a default value is used.
     * This is used to sync the audio and video stream.
     */
    EpicRtcStringView _streamId;

    /**
     * Desired audio bitrate.
     */
    uint32_t _bitrate;

    /**
     * Number of channels, e.g. mono or stereo.
     */
    uint8_t _channels;

    /**
     * Indicates source direction.
     */
    EpicRtcMediaSourceDirection _direction;
};

static_assert(sizeof(EpicRtcAudioSource) == 24);  // Ensure EpicRtcAudioSource is expected size on all platforms
#pragma pack(pop)
