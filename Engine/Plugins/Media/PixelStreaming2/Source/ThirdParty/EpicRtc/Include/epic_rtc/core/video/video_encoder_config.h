// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/core/video/video_common.h"
#include "epic_rtc/core/video/video_frame.h"
#include "epic_rtc/core/video/video_rate_control.h"

#pragma pack(push, 8)

/**
 * Video encoder configuration. This encompasses all settings, general and codec specific.
 * Expect this to be used with `SetConfig` to both initial initialization and consecutive configuration updates.
 */
struct EpicRtcVideoEncoderConfig
{
    /**
     * Number of CPU cores that API would like encoder to use.
     * Setting this to 1 will most likely bound the CPU based encoder to be synchronous.
     */
    int32_t _numberOfCores;

    /**
     * This is a maximum size for the encoded chunk.
     * As an example for H264 running in packetization-mode=0 (frame described with multiple NALUs F1(SPS|PPS|IDR|IDR|IDR),F2(SLICE|SLICE|SLICE))
     * this would be the maximum size of the single NAL unit that can then be wrapped into RTP packet and meet the MTU requirements.
     */
    uint64_t _maxPayloadSize;

    /**
     * Scalability mode as described in https://www.w3.org/TR/webrtc-svc/#scalabilitymodes*
     */
    EpicRtcVideoScalabilityMode _scalabilityMode = EpicRtcVideoScalabilityMode::None;

    /**
     * Codec type.
     */
    EpicRtcVideoCodec _codec;

    /**
     * Expected width of the input frame.
     */
    uint32_t _width;

    /**
     * Expected height of the input frame.
     */
    uint32_t _height;

    /**
     * Starting bitrate in kbps
     */
    uint32_t _startBitrate;

    /**
     * Upper bound for the bitrate in kbps.
     */
    uint32_t _maxBitrate;

    /**
     * Lower bound for the bitrate in kbps.
     */
    uint32_t _minBitrate;

    /**
     * Upper bound for the frame rate in fps.
     */
    uint32_t _maxFramerate;

    /**
     * Lower bound for the quantizer scale.
     */
    uint32_t _qpMin;

    /**
     * Upper bound for the quantizer scale.
     */
    uint32_t _qpMax;

    /**
     * Turns on de-noising support if encoder supports it.
     */
    EpicRtcBool _isDenoisingOn;

    /**
     * Turns on automatic resize of the input frame.
     */
    EpicRtcBool _isAutomaticResizeOn;

    /**
     * Specifies desired key frame interval.
     */
    int32_t _keyFrameInterval;

    /**
     * Turns on adaptive quantizers.
     */
    EpicRtcBool _isAdaptiveQpMode;

    /**
     * Indicates if the encoder should be operating in flexible mode
     */
    EpicRtcBool _isFlexibleMode;

    /**
     * Specifies inter layer prediction mode.
     */
    EpicRtcVideoInterLayerPredictionMode _interLayerPred;

    EpicRtcVideoRateControlParameters _rateControl;

    /**
     * Specifies the number of spatial layers
     */
    uint8_t _numberOfSpatialLayers;

    /**
     * Specifies the number of temporal layers
     */
    uint8_t _numberOfTemporalLayers;

    /**
     * Specifies the number of simulcast streams the encoder is expected to encode
     */
    uint8_t _numberOfSimulcastStreams;

    /**
     * Array interface that contains information regarding each simulcast stream if _numberOfSimulcastStreams is greater than 1
     */
    EpicRtcSpatialLayerArrayInterface* _simulcastStreams;

    /**
     * Array interface that contains information regarding each spatial layer for use with SVC
     */
    EpicRtcSpatialLayerArrayInterface* _spatialLayers;
};
static_assert(sizeof(EpicRtcVideoEncoderConfig) == 128);  // Ensure EpicRtcVideoEncoderConfig is expected size on all platforms

#pragma pack(pop)
