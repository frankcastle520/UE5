// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/core/audio/audio_codec_info.h"

#pragma pack(push, 8)

/**
 * Describes the current configuration of an Audio Decoders State
 */
struct EpicRtcAudioDecoderConfig
{
    EpicRtcAudioCodecInfo _codecInfo;
};

static_assert(sizeof(EpicRtcAudioDecoderConfig) == 56);  // Ensure EpicRtcAudioDecoderConfig is expected size on all platforms

#pragma pack(pop)
