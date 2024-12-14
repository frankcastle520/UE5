// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/core/video/video_buffer.h"
#include "epic_rtc/core/video/video_common.h"

#pragma pack(push, 8)

struct EpicRtcVideoFrame
{
    uint16_t _id;
    int64_t _timestampUs;
    int64_t _timestampRtp;
    // TODO(Nazar.Rudenko): This is to be able to use inbuilt video codecs, remove once we have codec implementation of our own, or introduce extra buffer types if we are to keeping inbuilt ones.
    EpicRtcBool _isBackedByWebRtc;
    EpicRtcVideoBufferInterface* _buffer;
};
static_assert(sizeof(EpicRtcVideoFrame) == 40);  // Ensure EpicRtcVideoFrame is expected size on all platforms

struct EpicRtcEncodedVideoFrame
{
    int32_t _width;
    int32_t _height;
    int64_t _timestampUs;
    int64_t _timestampRtp;
    EpicRtcVideoFrameType _frameType;
    int32_t _qp;
    EpicRtcEncodedVideoBufferInterface* _buffer;
    int32_t _spatialIndex;
    EpicRtcBool _hasSpatialIndex = false;
    int32_t _temporalIndex;
    EpicRtcBool _hasTemporalIndex = false;
    EpicRtcVideoCodec _videoCodec = EpicRtcVideoCodec::Unknown;
};
static_assert(sizeof(EpicRtcEncodedVideoFrame) == 64);  // Ensure EpicRtcEncodedVideoFrame is expected size on all platforms

#pragma pack(pop)
