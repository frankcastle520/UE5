// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/audio/audio_format.h"

#include "epic_rtc/containers/epic_rtc_span.h"

#pragma pack(push, 8)

/**
 * Audio codecs supported by the API values are FourCC inspired
 */
enum class EpicRtcAudioCodec : uint32_t
{
    Unknown = 0,
    Opus = EpicRtcCommon::FourValueEnum('O', 'P', 'U', 'S'), /*< Opus Codec */
                                              // ACC = FourValueEnum('A', 'C', 'C', '0')
};

/**
 * Describes features of an audio codec
 */
struct EpicRtcAudioCodecInfo
{
    /**
     * Codec being enoded
     */
    EpicRtcAudioCodec _codec = EpicRtcAudioCodec::Unknown;

    /**
     * Details of the audio format to be encoded
     */
    EpicRtcAudioFormat _audioFormat;

    /**
     *  Minimum supported bitrate of the codec
     */
    uint32_t _minBitrate = 0;

    /**
     * Max supported bitrate of the codec
     */
    uint32_t _maxBitrate = 0;

    /**
     * Target bitrate of the codec
     */
    uint32_t _targetBitrate = 0;

    /**
     * Frame lengths supported by the codec
     */
    EpicRtcInt32ArrayInterface* _supportedFrameLengthMs = nullptr;

    /**
     * If the codec will produce noise while the audio is silent
     */
    EpicRtcBool _allowComfortNoise = false;

    /**
     * If the codec supports network adaption of bitrates
     */
    EpicRtcBool _supportsNetworkAdaption = false;
};

static_assert(sizeof(EpicRtcAudioCodecInfo) == 56);  // Ensure EpicRtcAudioCodecInfo is expected size on all platforms

inline EpicRtcBool operator==(const EpicRtcAudioCodecInfo& lhs, const EpicRtcAudioCodecInfo& rhs)
{
    return lhs._codec == rhs._codec && lhs._audioFormat == rhs._audioFormat /* && LHS.bAllowComfortNoise == RHS.bAllowComfortNoise && LHS.bSupportsNetworkAdaption == RHS.bSupportsNetworkAdaption */;
}

#pragma pack(pop)
