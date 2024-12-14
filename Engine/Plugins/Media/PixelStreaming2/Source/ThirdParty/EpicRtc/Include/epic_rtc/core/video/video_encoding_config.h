// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/video/video_common.h"

#include <cstdint>

#pragma pack(push, 8)

/**
 * Encoder configuration for the FVideoSource.
 */
struct EpicRtcVideoEncodingConfig
{
    /**
     * Encoding variant id that will show in SDP (a=rid:RID).
     */
    EpicRtcStringView _rid;

    /**
     * Scaling factor.
     */
    double _scaleResolutionDownBy;

    /**
     * Scalability mode as described in https://www.w3.org/TR/webrtc-svc/#scalabilitymodes*
     */
    EpicRtcVideoScalabilityMode _scalabilityMode = EpicRtcVideoScalabilityMode::None;

    /**
     * Minimum bitrate for the encoder, bps.
     */
    uint32_t _minBitrate;

    /**
     * Maximum bitrate for the encoder, bps.
     */
    uint32_t _maxBitrate;

    /**
     * Maximum frame rate.
     */
    uint8_t _maxFrameRate;
};

static_assert(sizeof(EpicRtcVideoEncodingConfig) == 40);  // Ensure EpicRtcVideoEncodingConfig is expected size on all platforms

#pragma pack(pop)
