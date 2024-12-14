// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/video/video_common.h"
#include "epic_rtc/core/video/video_frame.h"

#pragma pack(push, 8)

class EpicRtcVideoDecoderCallbackInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API void Decoded(const EpicRtcVideoFrame& frame, const uint64_t decodeTimeMs, const uint8_t qp) = 0;

    EpicRtcVideoDecoderCallbackInterface(const EpicRtcVideoDecoderCallbackInterface&) = delete;
    EpicRtcVideoDecoderCallbackInterface& operator=(const EpicRtcVideoDecoderCallbackInterface&) = delete;

protected:
    EMRTC_API EpicRtcVideoDecoderCallbackInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoDecoderCallbackInterface() = default;
};

#pragma pack(pop)
