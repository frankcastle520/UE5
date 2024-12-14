// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#pragma pack(push, 8)

/**
 * Describes the signalling type to used
 */
enum class EpicRtcSignallingType : uint8_t
{
    /**
     * The reference signalling type. Used for tests
     */
    Reference,

    /**
     * The Rtcp compatible signalling type. Used by rtcp applications
     */
    Rtcp,

    /**
     * The Pixel Streaming compatible signalling type. Used by the Pixel Streaming Unreal Engine plugin
     */
    PixelStreaming,

    /**
     * Signalling for P2P communication using Entropy services.
     */
    EntropyP2P,
};

#pragma pack(pop)
