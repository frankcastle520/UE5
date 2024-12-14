// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/core/video/video_common.h"

#pragma pack(push, 8)

struct EpicRtcVideoDecoderConfig
{
    // indicative, can be ignored
    int32_t _bufferPoolSize = 0;
    // should not output frames larger than the specified resolution
    EpicRtcVideoResolution _maxOutputResolution;
    // for software decoders, max allowed number of cores to use.
    int32_t _numberOfCores = 1;
    EpicRtcVideoCodec _codec = EpicRtcVideoCodec::Unknown;
    EpicRtcBool _isHWAccelerated;
};
static_assert(sizeof(EpicRtcVideoDecoderConfig) == 24);  // Ensure EpicRtcVideoDecoderConfig is expected size on all platforms

#pragma pack(pop)
