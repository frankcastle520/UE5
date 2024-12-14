// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cassert>
#include <cstdint>

#pragma pack(push, 8)

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcStringView
{
    /**
     * Raw pointer to the data.
     */
    const char* _ptr;

    /**
     * Size of the data in characters.
     */
    uint64_t _length;
};

static_assert(sizeof(EpicRtcStringView) == 16);  // Ensure is expected size on all platforms

/**
 * Represents a key-value parameter pair
 */
struct EpicRtcParameterPair
{
    EpicRtcStringView _key;

    EpicRtcStringView _value;
};
static_assert(sizeof(EpicRtcParameterPair) == 32);  // Ensure EpicRtcParameterPair is expected size on all platforms

#pragma pack(pop)
