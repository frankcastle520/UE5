// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"
#include <cassert>
#include <cstdint>

#pragma pack(push, 8)

/**
 * Represents Track information used in signalling to tie track to media layer
 */
struct EpicRtcTrackInfo
{
    /**
     * Track id.
     * For Data channel this would be a label
     */
    EpicRtcStringView _id;

    /**
     * Stream id.
     */
    EpicRtcStringView _streamId;

    /**
     * SDP Mid of this track
     */
    EpicRtcStringView _mid;

    /**
     * Track's ssrc availability flag
     * True if ssrc value is available, otherwise false
     */
    EpicRtcBool _hasSsrc;

    /**
     * Track's ssrc
     */
    uint32_t _ssrc;

    /**
     * Track's state
     */
    EpicRtcTrackState _state;

    /**
     * Track's type
     */
    EpicRtcTrackType _type;
};

static_assert(sizeof(EpicRtcTrackInfo) == 3 * 16 + 8 + 4 + 2 * 2);  // Ensure EpicRtcTrackInfo is expected size on all platforms

#pragma pack(pop)
