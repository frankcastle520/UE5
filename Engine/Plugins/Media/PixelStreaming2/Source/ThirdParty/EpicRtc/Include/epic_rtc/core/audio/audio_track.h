// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/containers/epic_rtc_string.h"

#include "epic_rtc/core/audio/audio_frame.h"

#pragma pack(push, 8)

/**
 * Represents the audio track. Exposes methods to send and receive audio data.
 */
class EpicRtcAudioTrackInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets instance Id.
     * @return Id.
     */
    virtual EMRTC_API EpicRtcStringView GetId() = 0;

    /**
     * Mute/unmute the track.
     * @param InMuted State, pass true to mute, false to unmute.
     */
    virtual EMRTC_API void Mute(EpicRtcBool inMuted) = 0;

    /**
     * Stop track. Works with the local tracks only.
     */
    virtual EMRTC_API void Stop() = 0;

    /**
     * Subscribe to remote track.
     */
    virtual EMRTC_API void Subscribe() = 0;

    /**
     * Unsubscribe from remote track.
     */
    virtual EMRTC_API void Unsubscribe() = 0;

    /**
     * Pop frame for processing.
     * @param OutFrame Frame to populate.
     * @return False if no frame was avaliable or failed to pop the frame
     */
    virtual EMRTC_API EpicRtcBool PopFrame(EpicRtcAudioFrame& outFrame) = 0;

    /**
     * Supply frame for processing. This will push the frame onto the ADM pipeline or directly to the encoder.
     * Use ADM to push `main` audio source (such as microphone) as this will go through processing (AGC, EC, NS...).
     * @param InFrame Frame to process.
     * @param bBypassADM If true will push the frame onto track's encoder bypassing ADM.
     * @return False if error pushing frame
     */
    virtual EMRTC_API EpicRtcBool PushFrame(const EpicRtcAudioFrame& inFrame, EpicRtcBool bypassAdm) = 0;

    /**
     * Indicates the track belongs to the remote participant.
     * @return True if the track belongs to the remote participant.
     */
    virtual EMRTC_API EpicRtcBool IsRemote() = 0;

    /**
     * Gets track state.
     * @return State of the track.
     */
    virtual EMRTC_API EpicRtcTrackState GetState() = 0;

    /**
     * Get track subscription state.
     * @return Subscription state of the track.
     */
    virtual EMRTC_API EpicRtcTrackSubscriptionState GetSubscriptionState() = 0;

    // Prevent copying
    EpicRtcAudioTrackInterface(const EpicRtcAudioTrackInterface&) = delete;
    EpicRtcAudioTrackInterface& operator=(const EpicRtcAudioTrackInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcAudioTrackInterface() = default;
    virtual EMRTC_API ~EpicRtcAudioTrackInterface() = default;
};

#pragma pack(pop)
