// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/audio/audio_format.h"
#include "epic_rtc/core/audio/audio_frame.h"
#include "epic_rtc/core/audio/audio_decoder_config.h"

#pragma pack(push, 8)

/**
 * Interface to describe a EpicRTC compatible Audio Decoder
 */
class EpicRtcAudioDecoderInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get uniquely identifiable Encoder implementation name
     */
    virtual EMRTC_API EpicRtcStringView GetName() const = 0;

    /**
     * Get current configuration of decoder instance
     */
    virtual EMRTC_API EpicRtcAudioDecoderConfig GetConfig() const = 0;

    /**
     * Set configuration of decoder instance
     * @note be careful when manually setting this as it is likely set automatically internal of the API
     */
    virtual EMRTC_API EpicRtcMediaResult SetConfig(const EpicRtcAudioDecoderConfig& inAudioDecoderConfig) = 0;

    /**
     * Function that does actual encoding of audio expected to be blocking and synchronous
     * @return EpicRtcAudioFrame memory could be accessed asynchronously so memory should only be deallocated with Release method
     */
    virtual EMRTC_API EpicRtcAudioFrame Decode(EpicRtcEncodedAudioFrame& inEncodedAudioFrame) = 0;

    /**
     * Resets decoder to zeroed state
     */
    virtual EMRTC_API void Reset() = 0;

    /**
     * Internal usage only, overload if you know what you are doing
     */
    virtual EMRTC_API EpicRtcBool IsInbuilt() const { return false; }
};

/**
 * Describes how to initialize a custom Audio Encoder that has passed into EpicRTC
 */
class EpicRtcAudioDecoderInitializerInterface : public EpicRtcRefCountInterface
{
public:
    virtual EMRTC_API EpicRtcErrorCode CreateDecoder(const EpicRtcAudioCodecInfo& codecInfo, EpicRtcAudioDecoderInterface** outDecoder) = 0;
    virtual EMRTC_API EpicRtcAudioCodecInfoArrayInterface* GetSupportedCodecs() = 0;
};

#pragma pack(pop)
