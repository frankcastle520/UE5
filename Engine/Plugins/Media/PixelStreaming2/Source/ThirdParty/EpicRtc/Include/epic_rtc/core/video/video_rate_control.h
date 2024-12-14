// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/containers/epic_rtc_span.h"

#pragma pack(push, 8)

// TODO(Nazar.Rudenko): Make this ref counted
class EpicRtcVideoBitrateAllocationInterface
{
public:
    virtual EMRTC_API EpicRtcBool HasBitrate(uint64_t spatialIndex, uint64_t temporalIndex) const = 0;

    virtual EMRTC_API uint32_t GetBitrate(uint64_t spatialIndex, uint64_t temporalIndex) const = 0;

    virtual EMRTC_API EpicRtcBool IsSpatialLayerUsed(uint64_t spatialIndex) const = 0;

    virtual EMRTC_API uint32_t GetSpatialLayerSum(uint64_t spatialIndex) const = 0;

    virtual EMRTC_API uint32_t GetTemporalLayerSum(uint64_t spatialIndex, uint64_t temporalIndex) const = 0;

    virtual EMRTC_API EpicRtcUint32Span GetTemporalLayerAllocation(uint64_t spatialIndex) const = 0;

    virtual EMRTC_API uint32_t GetSumBps() const = 0;

    virtual EMRTC_API EpicRtcBool IsBwLimited() const = 0;
};

struct EpicRtcVideoRateControlParameters
{
    /**
     * Target bitrate, per spatial/temporal layer.
     * A target bitrate of 0bps indicates a layer should not be encoded at all.
     */
    EpicRtcVideoBitrateAllocationInterface* _targetBitrate;

    /**
     * Adjusted target bitrate, per spatial/temporal layer. May be lower or
     * higher than the target depending on encoder behaviour.
     */
    EpicRtcVideoBitrateAllocationInterface* _bitrate;

    /**
     * Target framerate, in fps. A value <= 0.0 is invalid and should be
     * interpreted as framerate target not available. In this case the encoder
     * should fall back to the max framerate specified in `codec_settings` of
     * the last InitEncode() call.
     */
    double _framerateFps;

    /**
     * The network bandwidth available for video.
     */
    uint64_t _bandwidthAllocationBps;
};
static_assert(sizeof(EpicRtcVideoRateControlParameters) == 32);  // Ensure EpicRtcVideoRateControlParameters is expected size on all platforms

#pragma pack(pop)
