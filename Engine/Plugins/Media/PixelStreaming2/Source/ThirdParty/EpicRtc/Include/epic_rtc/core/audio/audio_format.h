// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/containers/epic_rtc_array.h"

#pragma pack(push, 8)
/**
 * Describes the audio features used by the codecs
 */
struct EpicRtcAudioFormat
{
    /**
     * SampleRate in Hz
     */
    uint32_t _sampleRate;

    /**
     * Number of channels
     */
    uint32_t _numChannels;

    /**
     * Key Value pairs of additional parameters for use with the Codec
     */
    EpicRtcParameterPairArrayInterface* _parameters = nullptr;
};
static_assert(sizeof(EpicRtcAudioFormat) == 16);  // Ensure EpicRtcAudioFormat is expected size on all platforms

inline EpicRtcBool operator==(const EpicRtcAudioFormat& lhs, const EpicRtcAudioFormat& rhs)
{
    return lhs._sampleRate == rhs._sampleRate && lhs._numChannels == rhs._numChannels;
}
#pragma pack(pop)
