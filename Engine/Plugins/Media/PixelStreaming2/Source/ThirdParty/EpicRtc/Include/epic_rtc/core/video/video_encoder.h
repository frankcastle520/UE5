// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/core/video/video_encoder_config.h"
#include "epic_rtc/core/video/video_encoder_callback.h"

#pragma pack(push, 8)

class EpicRtcVideoEncoderInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API EpicRtcStringView GetName() const = 0;
    virtual EMRTC_API EpicRtcVideoEncoderConfig GetConfig() const = 0;
    virtual EMRTC_API EpicRtcMediaResult SetConfig(const EpicRtcVideoEncoderConfig& videoEncoderConfig) = 0;
    virtual EMRTC_API EpicRtcVideoEncoderInfo GetInfo() = 0;
    virtual EMRTC_API EpicRtcMediaResult Encode(const EpicRtcVideoFrame& videoFrame, EpicRtcVideoFrameTypeArrayInterface* frameTypes) = 0;
    virtual EMRTC_API void RegisterCallback(EpicRtcVideoEncoderCallbackInterface* callback) = 0;
    virtual EMRTC_API void Reset() = 0;

    EpicRtcVideoEncoderInterface(const EpicRtcVideoEncoderInterface&) = delete;
    EpicRtcVideoEncoderInterface& operator=(const EpicRtcVideoEncoderInterface&) = delete;

protected:
    EpicRtcVideoEncoderInterface() = default;
    virtual ~EpicRtcVideoEncoderInterface() = default;
};

class EpicRtcVideoEncoderInitializerInterface : public EpicRtcRefCountInterface
{
public:
    // TODO(Nazar.Rudenko): return should be an EpicRtcError, change once enum is available for use
    virtual EMRTC_API void CreateEncoder(EpicRtcVideoCodecInfoInterface* codecInfo, EpicRtcVideoEncoderInterface** outEncoder) = 0;
    virtual EMRTC_API EpicRtcStringView GetName() = 0;
    virtual EMRTC_API EpicRtcVideoCodecInfoArrayInterface* GetSupportedCodecs() = 0;

    EpicRtcVideoEncoderInitializerInterface(const EpicRtcVideoEncoderInitializerInterface&) = delete;
    EpicRtcVideoEncoderInitializerInterface& operator=(const EpicRtcVideoEncoderInitializerInterface&) = delete;

protected:
    EMRTC_API EpicRtcVideoEncoderInitializerInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoEncoderInitializerInterface() = default;
};

#pragma pack(pop)
