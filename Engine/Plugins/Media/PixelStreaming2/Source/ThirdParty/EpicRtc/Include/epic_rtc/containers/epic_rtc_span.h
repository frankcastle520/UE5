// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cassert>
#include <cstdint>

#include "epic_rtc_string_view.h"

#pragma pack(push, 8)

// Forward declare
struct EpicRtcVideoEncodingConfig;
class EpicRtcAudioEncoderInitializerInterface;
class EpicRtcAudioDecoderInitializerInterface;
class EpicRtcVideoEncoderInitializerInterface;
class EpicRtcVideoDecoderInitializerInterface;
enum class EpicRtcPixelFormat : uint8_t;
struct EpicRtcVideoResolutionBitrateLimits;
struct EpicRtcIceServer;
struct EpicRtcConnectionStats;
struct EpicRtcRoomStats;
struct EpicRtcSessionStats;
struct EpicRtcLocalTrackRtpStats;
struct EpicRtcLocalAudioTrackStats;
struct EpicRtcLocalVideoTrackStats;
struct EpicRtcRemoteTrackStats;
struct EpicRtcDataTrackStats;
struct EpicRtcIceCandidateStats;
struct EpicRtcIceCandidatePairStats;
struct EpicRtcTransportStats;
struct EpicRtcCertificateStats;

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcStringViewSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcStringView* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcStringViewSpan) == 16);  // Ensure EpicRtcStringViewSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcVideoEncodingConfigSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcVideoEncodingConfig* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcVideoEncodingConfigSpan) == 16);  // Ensure EpicRtcVideoEncodingConfigSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcAudioEncoderInitializerSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcAudioEncoderInitializerInterface** _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcAudioEncoderInitializerSpan) == 16);  // Ensure EpicRtcVideoEncodingConfigSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcAudioDecoderInitializerSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcAudioDecoderInitializerInterface** _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcAudioDecoderInitializerSpan) == 16);  // Ensure EpicRtcVideoEncodingConfigSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcVideoEncoderInitializerInterfaceSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcVideoEncoderInitializerInterface** _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcVideoEncoderInitializerInterfaceSpan) == 16);  // Ensure EpicRtcVideoEncoderInitializerInterfaceSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcVideoDecoderInitializerInterfaceSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcVideoDecoderInitializerInterface** _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcVideoDecoderInitializerInterfaceSpan) == 16);  // Ensure EpicRtcVideoDecoderInitializerInterfaceSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcUint32Span
{
    /**
     * Raw pointer to the data.
     */
    const uint32_t* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcUint32Span) == 16);  // Ensure EpicRtcUint32Span is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcIceServerSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcIceServer* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcIceServerSpan) == 16);  // Ensure EpicRtcIceServerSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcConnectionStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcConnectionStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcConnectionStatsSpan) == 16);  // Ensure EpicRtcConnectionStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcLocalTrackRtpStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcLocalTrackRtpStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcLocalTrackRtpStatsSpan) == 16);  // Ensure EpicRtcLocalTrackRtpStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcLocalAudioTrackStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcLocalAudioTrackStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcLocalAudioTrackStatsSpan) == 16);  // Ensure EpicRtcLocalAudioTrackStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcLocalVideoTrackStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcLocalVideoTrackStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcLocalVideoTrackStatsSpan) == 16);  // Ensure EpicRtcLocalVideoTrackStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcRemoteTrackStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcRemoteTrackStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcRemoteTrackStatsSpan) == 16);  // Ensure EpicRtcRemoteTrackStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcDataTrackStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcDataTrackStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcDataTrackStatsSpan) == 16);  // Ensure EpicRtcDataTrackStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcIceCandidateStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcIceCandidateStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcIceCandidateStatsSpan) == 16);  // Ensure EpicRtcIceCandidateStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcIceCandidatePairStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcIceCandidatePairStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcIceCandidatePairStatsSpan) == 16);  // Ensure EpicRtcIceCandidatePairStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcTransportStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcTransportStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcTransportStatsSpan) == 16);  // Ensure EpicRtcTransportStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcCertificateStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcCertificateStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcCertificateStatsSpan) == 16);  // Ensure EpicRtcCertificateStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcRoomStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcRoomStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcRoomStatsSpan) == 16);  // Ensure EpicRtcRoomStatsSpan is expected size on all platforms

/**
 * Represents a view into a contiguous sequence of memory. The struct is non owning.
 */
struct EpicRtcSessionStatsSpan
{
    /**
     * Raw pointer to the data.
     */
    const EpicRtcSessionStats* _ptr;

    /**
     * Number of elements in the Span.
     */
    uint64_t _size;
};

static_assert(sizeof(EpicRtcSessionStatsSpan) == 16);  // Ensure EpicRtcSessionStatsSpan is expected size on all platforms

#pragma pack(pop)
