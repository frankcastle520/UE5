// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/common/defines.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

/**
 * Represents SDP
 */
class EpicRtcSdpInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Indicates SDP type
     */
    virtual EMRTC_API EpicRtcSdpType GetType() const = 0;

    /**
     * SDP representation
     */
    virtual EMRTC_API EpicRtcStringView GetSdp() const = 0;

    // Prevent copying
    EpicRtcSdpInterface(const EpicRtcSdpInterface&) = delete;
    EpicRtcSdpInterface& operator=(const EpicRtcSdpInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcSdpInterface() = default;
    virtual EMRTC_API ~EpicRtcSdpInterface() = default;
};

#pragma pack(pop)
