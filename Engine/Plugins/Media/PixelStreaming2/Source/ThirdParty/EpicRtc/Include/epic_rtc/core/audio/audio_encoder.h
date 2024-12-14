// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/audio/audio_format.h"
#include "epic_rtc/core/audio/audio_frame.h"
#include "epic_rtc/core/audio/audio_encoder_config.h"


#pragma pack(push, 8)

/**
 * Interface to describe a EpicRTC compatible Audio Encoder
 */
class EpicRtcAudioEncoderInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get uniquely identifiable Encoder implementation name
     */
    virtual EMRTC_API EpicRtcStringView GetName() const = 0;

    /**
     * Get current configuration of Encoder instance
     */
    virtual EMRTC_API const EpicRtcAudioEncoderConfig& GetAudioEncoderConfig() const = 0;

    /**
     * Set configuration of Encoder instance
     * @note be careful when manually setting this as it is likely set automatically internal of the API
     */
    virtual EMRTC_API EpicRtcMediaResult SetAudioEncoderConfig(const EpicRtcAudioEncoderConfig& inAudioDecoderConfig) = 0;

    /**
     * Function that does actual encoding of audio expected to be blocking and synchronous
     * @return EpicRtcEncodedAudioFrame memory could be accessed asynchronously so memory should only be deallocated with Release method
     */
    virtual EMRTC_API EpicRtcEncodedAudioFrame Encode(EpicRtcAudioFrame& inAudioFrame) = 0;

    /**
     * Resets the Encoder to a zeroed state ready for more encoding
     */
    virtual EMRTC_API void Reset() = 0;
};

/**
 * Describes how to initialize a custom Audio Encoder that has passed into EpicRTC
 */
class EpicRtcAudioEncoderInitializerInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API EpicRtcErrorCode CreateEncoder(const EpicRtcAudioCodecInfo& codecInfo, int32_t payLoad, EpicRtcAudioEncoderInterface** outEncoder) = 0;
    virtual EMRTC_API EpicRtcAudioCodecInfoArrayInterface* GetSupportedCodecs() = 0;
    virtual EMRTC_API EpicRtcMediaResult QueryAudioEncoder(const EpicRtcAudioCodecInfo& codecInfo, EpicRtcAudioCodecInfo& outCodecInfo) = 0;
};

#pragma pack(pop)
