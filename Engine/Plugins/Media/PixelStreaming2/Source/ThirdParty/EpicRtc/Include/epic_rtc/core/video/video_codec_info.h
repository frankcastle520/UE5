// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"

#include "epic_rtc/containers/epic_rtc_array.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/video/video_common.h"

#pragma pack(push, 8)

class EpicRtcVideoCodecInfoInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Codec name.
     */
    virtual EMRTC_API EpicRtcVideoCodec GetCodec() = 0;

    /**
     * Codec specific parameters that might be translated to sdp.
     * As an example, for H264 this might be the profile-level or packetization-mode.
     */
    virtual EMRTC_API EpicRtcParameterPairArrayInterface* GetParameters() = 0;

    /**
     * Scalability modes this codec supports.
     */
    virtual EMRTC_API EpicRtcVideoScalabilityModeArrayInterface* GetScalabilityModes() = 0;

    /**
     * Indicates that the codec is hardware accelerated.
     */
    virtual EMRTC_API EpicRtcBool IsHardwareAccelerated() = 0;

    EpicRtcVideoCodecInfoInterface(const EpicRtcVideoCodecInfoInterface&) = delete;
    EpicRtcVideoCodecInfoInterface& operator=(const EpicRtcVideoCodecInfoInterface&) = delete;

protected:
    EMRTC_API EpicRtcVideoCodecInfoInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoCodecInfoInterface() = default;
};

#pragma pack(pop)
