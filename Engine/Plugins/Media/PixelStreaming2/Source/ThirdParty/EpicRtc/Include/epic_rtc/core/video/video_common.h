// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_span.h"
#include "epic_rtc/containers/epic_rtc_array.h"
#include "epic_rtc/containers/epic_rtc_string.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/video/video_frame_dependency.h"
#include "epic_rtc/core/video/video_frame_info.h"

#pragma pack(push, 8)

/**
 *
 */
// TODO(Eden.Harris) String would allow us support more codecs without changing the public interface
enum class EpicRtcVideoCodec : uint32_t
{
    Unknown = 0,
    H264 = EpicRtcCommon::FourValueEnum('H', '2', '6', '4'),
    /*
    H265 = EpicRtcCommon::FourValueEnum('H', '2', '6', '5'),
    H266 = EpicRtcCommon::FourValueEnum('H', '2', '6', '6'),
    */
    VP8 = EpicRtcCommon::FourValueEnum('V', 'P', '8', 0),
    VP9 = EpicRtcCommon::FourValueEnum('V', 'P', '9', 0),
    AV1 = EpicRtcCommon::FourValueEnum('A', 'V', '1', 0),
};

/**
 *
 */
enum class EpicRtcPixelFormat : uint8_t
{
    Native = 0,
    I420,
    I420A,
    I422,
    I444,
    I010,
    I210,
    NV12,
};

/**
 *
 */
// TODO(Eden.Harris) String would allow us support more formats without changing the public interface
enum class EpicRtcVideoFrameType : char
{
    Unknown = 0,
    B = 'B',
    I = 'I',
    P = 'P',
};

enum class EpicRtcDecodeTargetIndication : uint8_t
{
    NotPresent = 0,   // DecodeTargetInfo symbol '-'
    Discardable = 1,  // DecodeTargetInfo symbol 'D'
    Switch = 2,       // DecodeTargetInfo symbol 'S'
    Required = 3      // DecodeTargetInfo symbol 'R'
};

struct EpicRtcVideoResolution
{
    int32_t _width;
    int32_t _height;
};
static_assert(sizeof(EpicRtcVideoResolution) == 8);  // Ensure EpicRtcVideoResolution is expected size on all platforms

struct EpicRtcVideoResolutionBitrateLimits
{
    // Size of video frame, in pixels, the bitrate thresholds are intended for.
    int32_t _frameSizePixels = 0;
    // Recommended minimum bitrate to start encoding.
    int32_t _minStartBitrateBps = 0;
    // Recommended minimum bitrate.
    int32_t _minBitrateBps = 0;
    // Recommended maximum bitrate.
    int32_t _maxBitrateBps = 0;
};
static_assert(sizeof(EpicRtcVideoResolutionBitrateLimits) == 16);  // Ensure EpicRtcVideoResolutionBitrateLimits is expected size on all platforms

struct EpicRtcSpatialLayer
{
    EpicRtcVideoResolution _resolution;
    uint32_t _maxFramerate = 0;  // fps.
    uint8_t _numberOfTemporalLayers = 1;
    uint32_t _maxBitrate = 0;     // kilobits/sec.
    uint32_t _targetBitrate = 0;  // kilobits/sec.
    uint32_t _minBitrate = 0;     // kilobits/sec.
    uint32_t _qpMax = 0;          // minimum quality
    EpicRtcBool _active = false;  // encoded and sent.
};
static_assert(sizeof(EpicRtcSpatialLayer) == 36);  // Ensure EpicRtcSpatialLayer is expected size on all platforms

static const uint64_t EPIC_RTC_CODEC_SPECIFIC_INFO_VP8_BUFFER_SIZE = 3;
struct EpicRtcCodecSpecificInfoVP8
{
    EpicRtcBool _nonReference;
    uint8_t _temporalIdx;
    EpicRtcBool _layerSync;
    int8_t _keyIdx;
    EpicRtcBool _useExplicitDependencies;
    uint64_t _referencedBuffers[EPIC_RTC_CODEC_SPECIFIC_INFO_VP8_BUFFER_SIZE];
    uint64_t _referencedBuffersCount;
    uint64_t _updatedBuffers[EPIC_RTC_CODEC_SPECIFIC_INFO_VP8_BUFFER_SIZE];
    uint64_t _updatedBuffersCount;
};
static_assert(sizeof(EpicRtcCodecSpecificInfoVP8) == 72);  // Ensure EpicRtcCodecSpecificInfoVP8 is expected size on all platforms

static const uint64_t EPIC_RTC_GOF_INFO_VP9_MAX_FRAMES_IN_GOF = 0xFF;
static const uint64_t EPIC_RTC_CODEC_SPECIFIC_INFO_VP9_MAX_REF_PICS = 3;
struct EpicRtcGofInfoVP9
{
    uint64_t _numFramesInGof;
    uint8_t _temporalIdx[EPIC_RTC_GOF_INFO_VP9_MAX_FRAMES_IN_GOF];
    EpicRtcBool _temporalUpSwitch[EPIC_RTC_GOF_INFO_VP9_MAX_FRAMES_IN_GOF];
    uint8_t _numRefPics[EPIC_RTC_GOF_INFO_VP9_MAX_FRAMES_IN_GOF];
    uint8_t _pidDiff[EPIC_RTC_GOF_INFO_VP9_MAX_FRAMES_IN_GOF][EPIC_RTC_CODEC_SPECIFIC_INFO_VP9_MAX_REF_PICS];
    uint16_t _pidStart;
};
static_assert(sizeof(EpicRtcGofInfoVP9) == 1544);  // Ensure EpicRtcGofInfoVP9 is expected size on all platforms

static const uint64_t EPIC_RTC_CODEC_SPECIFIC_INFO_VP9_MAX_SPATIAL_LAYERS = 8;
struct EpicRtcCodecSpecificInfoVP9
{
    EpicRtcBool _firstFrameInPicture;  // First frame, increment picture_id.
    EpicRtcBool _interPicPredicted;    // This layer frame is dependent on previously
    // coded frame(s).
    EpicRtcBool _flexibleMode;
    EpicRtcBool _ssDataAvailable;
    EpicRtcBool _nonRefForInterLayerPred;

    uint8_t _temporalIdx;
    EpicRtcBool _temporalUpSwitch;
    EpicRtcBool _interLayerPredicted;  // Frame is dependent on directly lower spatial
    // layer frame.
    uint8_t _gofIdx;

    // SS data.
    uint64_t _numSpatialLayers;  // Always populated.
    uint64_t _firstActiveLayer;
    EpicRtcBool _spatialLayerResolutionPresent;
    uint16_t _width[EPIC_RTC_CODEC_SPECIFIC_INFO_VP9_MAX_SPATIAL_LAYERS];
    uint16_t _height[EPIC_RTC_CODEC_SPECIFIC_INFO_VP9_MAX_SPATIAL_LAYERS];
    EpicRtcGofInfoVP9 _gof;

    // Frame reference data.
    uint8_t _numRefPics;
    uint8_t _pDiff[EPIC_RTC_CODEC_SPECIFIC_INFO_VP9_MAX_REF_PICS];
};
static_assert(sizeof(EpicRtcCodecSpecificInfoVP9) == 1624);  // Ensure EpicRtcCodecSpecificInfoVP9 is expected size on all platforms

struct EpicRtcCodecSpecificInfoH264
{
    EpicRtcBool _isSingleNAL;
    uint8_t _temporalIdx;  // This should be 255U if temporal indexing is not used
    EpicRtcBool _baseLayerSync;
    EpicRtcBool _isIDR;  // Set only on IDR frames and not just ordinary I frames
};
static_assert(sizeof(EpicRtcCodecSpecificInfoH264) == 4);  // Ensure EpicRtcCodecSpecificInfoH264 is expected size on all platforms

struct EpicRtcCodecSpecificInfoAV1
{
    // TODO(Nazar.Rudenko): Fill this in
};
static_assert(sizeof(EpicRtcCodecSpecificInfoAV1) == 1);  // Ensure EpicRtcCodecSpecificInfoAV1 is expected size on all platforms

union EpicRtcCodecSpecificInfoUnion
{
    EpicRtcCodecSpecificInfoVP8 _vp8;
    EpicRtcCodecSpecificInfoVP9 _vp9;
    EpicRtcCodecSpecificInfoH264 _h264;
    EpicRtcCodecSpecificInfoAV1 _av1;
};
static_assert(sizeof(EpicRtcCodecSpecificInfoUnion) == 1624);  // Ensure EpicRtcCodecSpecificInfoUnion is expected size on all platforms

struct EpicRtcCodecSpecificInfo
{
    EpicRtcVideoCodec _codec = EpicRtcVideoCodec::Unknown;
    EpicRtcCodecSpecificInfoUnion _codecSpecific;
    EpicRtcBool _endOfPicture = true;
    EpicRtcGenericFrameInfoInterface* _genericFrameInfo;
    EpicRtcBool _hasGenericFrameInfo = false;
    EpicRtcFrameDependencyStructure* _templateStructure;
    EpicRtcBool _hasTemplateStructure = false;
};
static_assert(sizeof(EpicRtcCodecSpecificInfo) == 1672);  // Ensure EpicRtcCodecSpecificInfo is expected size on all platforms

/**
 * Scalability mode as described in https://www.w3.org/TR/webrtc-svc/#scalabilitymodes*
 */
enum class EpicRtcVideoScalabilityMode : uint8_t
{
    L1T1,
    L1T2,
    L1T3,
    L2T1,
    L2T1h,
    L2T1Key,
    L2T2,
    L2T2h,
    L2T2Key,
    L2T2KeyShift,
    L2T3,
    L2T3h,
    L2T3Key,
    L3T1,
    L3T1h,
    L3T1Key,
    L3T2,
    L3T2h,
    L3T2Key,
    L3T3,
    L3T3h,
    L3T3Key,
    S2T1,
    S2T1h,
    S2T2,
    S2T2h,
    S2T3,
    S2T3h,
    S3T1,
    S3T1h,
    S3T2,
    S3T2h,
    S3T3,
    S3T3h,
    None
};

enum class EpicRtcVideoInterLayerPredictionMode : uint8_t
{
    Off,
    On,
    OnKeyPicture
};

struct EpicRtcVideoEncoderInfo
{
    /**
     * The width and height of the incoming video frames should be divisible
     * by `requestedResolutionAlignment`. If they are not, the encoder may
     * drop the incoming frame.
     */
    int32_t _requestedResolutionAlignment;

    /**
     * Same as above but if true, each simulcast layer should also be divisible
     * by `requestedResolutionAlignment`.
     */
    int32_t _applyAlignmentToAllSimulcastLayers;

    /**
     * If true, encoder supports working with a native handle (e.g. texture
     * handle for hw codecs)
     */
    EpicRtcBool _supportsNativeHandle;

    EpicRtcVideoCodecInfoInterface* _codecInfo;

    /**
     * Recommended bitrate limits for different resolutions.
     */
    EpicRtcVideoResolutionBitrateLimitsArrayInterface* _resolutionBitrateLimits;

    /**
     * If true, this encoder has internal support for generating simulcast
     * streams. Otherwise, an adapter class will be needed.
     */
    EpicRtcBool _supportsSimulcast;

    /**
     * The list of pixel formats preferred by the encoder.
     */
    EpicRtcPixelFormatArrayInterface* _preferredPixelFormats;
};
static_assert(sizeof(EpicRtcVideoEncoderInfo) == 48);  // Ensure EpicRtcVideoEncoderInfo is expected size on all platforms

/**
 * Return type of the encoded callback.
 * Let's encoder know what happened to the produced bitstream.
 */
struct EpicRtcVideoEncodedResult
{
    /**
     * Indicates that bitstream wasn't consumed.
     */
    EpicRtcBool _error;

    /**
     * Frame id that was assigned to the produced frame.
     * When transport is RTP this will take a value of frame's timestamp.
     */
    uint32_t _frameId;

    /**
     * Indicates to the encoder that next frame should be dropped.
     */
    EpicRtcBool _dropNextFrame;
};
static_assert(sizeof(EpicRtcVideoEncodedResult) == 12);  // Ensure EpicRtcVideoEncodedResult is expected size on all platforms

enum class EpicRtcVideoFrameDropReason : uint8_t
{
    /**
     * Frame was dropped to meet the bitrate constraints.
     */
    DroppedByRateLimiter,

    /**
     * Frame was dropped by the encoder (e.g. previous encoder result indicated `_dropNextFrame == true`)
     */
    DroppedByEncoder
};

struct EpicRtcCodecBufferUsage
{
    int32_t _id = 0;
    EpicRtcBool _referenced = false;
    EpicRtcBool _updated = false;
};
static_assert(sizeof(EpicRtcCodecBufferUsage) == 8);  // Ensure EpicRtcCodecBufferUsage is expected size on all platforms

#pragma pack(pop)
