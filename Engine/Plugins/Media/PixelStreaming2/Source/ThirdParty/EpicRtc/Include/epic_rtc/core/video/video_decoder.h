// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/video/video_decoder_config.h"
#include "epic_rtc/core/video/video_decoder_callback.h"

#pragma pack(push, 8)

class EpicRtcVideoDecoderInterface : public EpicRtcRefCountInterface
{
public:
    [[nodiscard]] virtual EMRTC_API EpicRtcStringView GetName() const = 0;
    virtual EMRTC_API EpicRtcVideoDecoderConfig GetConfig() const = 0;
    virtual EMRTC_API EpicRtcMediaResult SetConfig(const EpicRtcVideoDecoderConfig& config) = 0;
    virtual EMRTC_API EpicRtcMediaResult Decode(const EpicRtcEncodedVideoFrame& frame) = 0;
    virtual EMRTC_API void RegisterCallback(EpicRtcVideoDecoderCallbackInterface* callback) = 0;
    virtual EMRTC_API void Reset() = 0;

    EpicRtcVideoDecoderInterface(const EpicRtcVideoDecoderInterface&) = delete;
    EpicRtcVideoDecoderInterface& operator=(const EpicRtcVideoDecoderInterface&) = delete;

protected:
    EMRTC_API EpicRtcVideoDecoderInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoDecoderInterface() = default;
};

class EpicRtcVideoDecoderInitializerInterface : public EpicRtcRefCountInterface
{
public:
    // TODO(Nazar.Rudenko): return should be an EpicRtcError, change once enum is available for use
    virtual EMRTC_API void CreateDecoder(EpicRtcVideoCodecInfoInterface* codecInfo, EpicRtcVideoDecoderInterface** outDecoder) = 0;
    virtual EMRTC_API EpicRtcStringView GetName() = 0;
    virtual EMRTC_API EpicRtcVideoCodecInfoArrayInterface* GetSupportedCodecs() = 0;

    EpicRtcVideoDecoderInitializerInterface(const EpicRtcVideoDecoderInitializerInterface&) = delete;
    EpicRtcVideoDecoderInitializerInterface& operator=(const EpicRtcVideoDecoderInitializerInterface&) = delete;

protected:
    EMRTC_API EpicRtcVideoDecoderInitializerInterface() = default;
    virtual EMRTC_API ~EpicRtcVideoDecoderInitializerInterface() = default;
};

#pragma pack(pop)
