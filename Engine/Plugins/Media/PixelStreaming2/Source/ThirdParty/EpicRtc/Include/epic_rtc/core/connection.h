// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"

#include "epic_rtc/core/audio/audio_source.h"
#include "epic_rtc/core/connection_config.h"
#include "epic_rtc/core/data_source.h"
#include "epic_rtc/core/video/video_source.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

/**
 * Represents media connection with the MediaGateway or another Participant. In terms of WebRTC, this would be PeerConnection.
 * Holds all the media-related state and methods.
 */
class EpicRtcConnectionInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Add audio source.
     * @param InAudioSource Audio source to be added.
     */
    virtual EMRTC_API void AddAudioSource(const EpicRtcAudioSource& inAudioSource) = 0;

    /**
     * Add video source.
     * @param InVideoSource Video source to be added.
     */
    virtual EMRTC_API void AddVideoSource(const EpicRtcVideoSource& inVideoSource) = 0;

    /**
     * Add data source.
     * @param InDataSource Data source to be added.
     */
    virtual EMRTC_API void AddDataSource(const EpicRtcDataSource& inDataSource) = 0;

    /**
     * Gets maximum frame size for data Track in bytes.
     * @return Maximum frame size.
     */
    virtual EMRTC_API uint64_t GetMaxDataMessageSizeBytes() = 0;

    /**
     * Restarts underlying transport after applying the new configuration.
     * In WebRTC terms, this would be the same as restarting ice.
     * @param InConnectionConfig New connection configuration.
     */
    virtual EMRTC_API void RestartConnection(const EpicRtcConnectionConfig& inConnectionConfig) = 0;

    /**
     * Sets negotiation mode. In manual mode consumer is responsible for starting the negotiation process.
     * In Auto mode negotiation will start automatically once user adds a track or the other side indicates that negotiation is needed.
     * @param bInManualNegotiation manual negotiation flag
     */
    virtual EMRTC_API void SetManualNegotiation(EpicRtcBool inManualNegotiation) = 0;

    /**
     * Start the negotiation with the remote peer. This has effect only in manual negotiation mode.
     */
    virtual EMRTC_API void StartNegotiation() = 0;

    /**
     * Sets the bitrates used for this connection. Default values are set in the EpicRtcRoomConfig during CreateRoom, but this method
     * can be used to update the rate on a per connection basis.
     * @param inBitrate New bitrate configuration.
     */
    virtual EMRTC_API void SetConnectionRates(const EpicRtcBitrate& inBitrate) = 0;

    /**
     * Stats toggle at Connection level, set to false to disable stats for this specific connection only.
     * @param enabled Enable/disable flag
     */
    virtual EMRTC_API void SetStatsEnabled(EpicRtcBool enabled) = 0;

    // Prevent copying
    EpicRtcConnectionInterface(const EpicRtcConnectionInterface&) = delete;
    EpicRtcConnectionInterface& operator=(const EpicRtcConnectionInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcConnectionInterface() = default;
    virtual EMRTC_API ~EpicRtcConnectionInterface() = default;
};

#pragma pack(pop)
