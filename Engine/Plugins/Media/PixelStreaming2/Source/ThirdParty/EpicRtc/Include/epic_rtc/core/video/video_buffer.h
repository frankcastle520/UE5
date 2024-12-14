// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cstdint>

#include "epic_rtc/common/defines.h"

#include "epic_rtc/core/video/video_common.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

class EpicRtcVideoBufferInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API void* GetData() = 0;
    virtual EMRTC_API EpicRtcPixelFormat GetFormat() = 0;
    virtual EMRTC_API int32_t GetWidth() = 0;
    virtual EMRTC_API int32_t GetHeight() = 0;

    EpicRtcVideoBufferInterface(const EpicRtcVideoBufferInterface&) = delete;
    EpicRtcVideoBufferInterface& operator=(const EpicRtcVideoBufferInterface&) = delete;

protected:
    EMRTC_API EpicRtcVideoBufferInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoBufferInterface() = default;
};

class EpicRtcEncodedVideoBufferInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API const uint8_t* GetData() const = 0;
    virtual EMRTC_API uint64_t GetSize() const = 0;

    EpicRtcEncodedVideoBufferInterface(const EpicRtcEncodedVideoBufferInterface&) = delete;
    EpicRtcEncodedVideoBufferInterface& operator=(const EpicRtcEncodedVideoBufferInterface&) = delete;

protected:
    EMRTC_API EpicRtcEncodedVideoBufferInterface() = default;
    virtual EMRTC_API ~EpicRtcEncodedVideoBufferInterface() = default;
};

#pragma pack(pop)
