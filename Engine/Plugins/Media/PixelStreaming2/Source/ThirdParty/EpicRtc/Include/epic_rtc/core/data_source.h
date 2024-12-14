// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"

#pragma pack(push, 8)
/**
 * Describes data source that can be added to EpicRtcConnectionInterface
 */
struct EpicRtcDataSource
{
    /**
     * EpicRtcDataTrackInterface label.
     */
    EpicRtcStringView _label;

    /**
     * The max period of time in milliseconds in which retransmissions will be
     * sent. After this time, no more retransmissions will be sent.
     */
    int32_t _maxRetransmitTime;

    /**
     * The max number of retransmissions.
     */
    int32_t _maxRetransmits;

    /**
     * Enforce packet ordering.
     */
    EpicRtcBool _isOrdered;

    /**
     * Underlying protocol implementation.
     */
    EpicRtcDataSourceProtocol _protocol;

    /**
     * Indicates that this data source was negotiated out of band
     */
    EpicRtcBool _negotiated;

    /**
     * Out of band negotiated source id
     */
    uint32_t _transportChannelId;
};

static_assert(sizeof(EpicRtcDataSource) == 32);  // Ensure EpicRtcDataSource is expected size on all platforms

#pragma pack(pop)
