// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/core/audio/audio_format.h"
#include "epic_rtc/core/audio/audio_codec_info.h"

#pragma pack(push, 8)

/**
 * Describes an uncompressed buffer of audio data
 */
struct EpicRtcAudioFrame
{
    /**
     * Pointer to the audio data
     */
    int16_t* _data = nullptr;

    /**
     * Number of frames in data.
     * Note that with stereo one frame will contain two samples.
     * To get the actual size of the _data multiply _length by number of channels.
     */
    uint32_t _length = 0;

    /**
     * Timestamp in milliseconds
     */
    uint32_t _timestamp = 0;

    /**
     * Format of the Audio in the buffer
     */
    EpicRtcAudioFormat _format;
};

static_assert(sizeof(EpicRtcAudioFrame) == 8 + 4 + 4 + 16);  // Ensure EpicRtcAudioFrame is expected size on all platforms

/**
 * Describes a compressed buffer of audio data
 */
struct EpicRtcEncodedAudioFrame
{
    /**
     * Pointer to the audio data
     */
    uint8_t* _data = nullptr;

    /**
     * Length of the Data in bytes
     */
    uint32_t _length = 0;

    /**
     * Timestamp in milliseconds
     */
    uint32_t _timestamp = 0;

    /**
     * Payload type.
     */
    int32_t _payloadType = 0;

    /**
     * Details of the Codec used to encode the audio data
     */
    EpicRtcAudioCodecInfo _codecInfo;
};

static_assert(sizeof(EpicRtcEncodedAudioFrame) == 80);  // Ensure EpicRtcEncodedAudioFrame is expected size on all platforms

#pragma pack(pop)
