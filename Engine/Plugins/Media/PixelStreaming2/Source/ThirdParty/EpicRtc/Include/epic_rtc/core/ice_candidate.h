// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/containers/epic_rtc_string_view.h"
#include <cassert>
#include <cstdint>

#pragma pack(push, 8)

/**
 * Represents ICE candidate
 */
struct EpicRtcIceCandidate
{
    /**
     * Mid this candidates attached to
     */
    EpicRtcStringView _mid;

    /**
     * Candidate representation
     */
    EpicRtcStringView _candidate;

    /**
     * SDP m-line index of this candidate
     */
    int32_t _mLineIndex;
};

static_assert(sizeof(EpicRtcIceCandidate) == 2 * 16 + 8);  // Ensure EpicRtcIceCandidate is expected size on all platforms

#pragma pack(pop)
