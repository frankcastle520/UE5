// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/common/common.h"
#include "epic_rtc/core/audio/audio_codec_info.h"

#pragma pack(push, 8)

enum class EpicRtcAudioEncoderApplication : uint8_t
{
    /**
     * Sending microphone input
     */
    Speech = 0,

    /**
     * Sending system/prerecorded audio
     */
    Audio
};

/**
 * Describes the current configuration of an Audio Encoders State
 */
struct EpicRtcAudioEncoderConfig
{
    /**
     * Description of the Codec
     */
    EpicRtcAudioCodecInfo _codecInfo;

    /**
     * Size in Ms of the next Frame the Encoder will produce. Likely static during an Encoders life cycle.
     */
    uint32_t _nextFrameLengthMs;

    /**
     * If Forward Error Correction is Enabled
     */
    EpicRtcBool _fecEnabled = false;

    /**
     * Indicates status of codec-internal DTX
     */
    EpicRtcBool _dtxEnabled = false;

    /**
     * Packet loss rate indicated by the transport layer.
     * This will enable the encoder to trigger progressively more loss resistant behavior.
     */
    float _packetLossRate = 0.0f;

    /**
     * Encoder's application
     */
    EpicRtcAudioEncoderApplication _application = EpicRtcAudioEncoderApplication::Speech;
};

static_assert(sizeof(EpicRtcAudioEncoderConfig) == 72);  // Ensure EpicRtcAudioEncoderConfig is expected size on all platforms

#pragma pack(pop)
