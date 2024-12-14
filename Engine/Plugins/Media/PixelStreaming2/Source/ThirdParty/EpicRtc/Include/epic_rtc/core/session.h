// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/room.h"
#include "epic_rtc/core/room_config.h"

#pragma pack(push, 8)

/**
 * Represents a session with the signaling server. It groups all the resources (Rooms) that were allocated by the server.
 */
class EpicRtcSessionInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets instance Id
     * @return Id
     */
    virtual EMRTC_API EpicRtcStringView GetId() = 0;

    /**
     * Initiates a connection to the signaling server.
     * @return EpicRtcErrorCode::Ok on success.
     */
    virtual EMRTC_API EpicRtcErrorCode Connect() = 0;

    /**
     * Disconnects the Session from the signaling server. This will free all the resources that were allocated during the Session lifetime.
     * @param reason Disconnect reason. May be a null string with 0 length or a valid string.
     * @return EpicRtcErrorCode::Ok on success.
     */
    virtual EMRTC_API EpicRtcErrorCode Disconnect(const EpicRtcStringView reason) = 0;

    /**
     * Creates Room object within this Session. Release must be called on the room else it will leak.
     * @param inConfig Room configuration.
     * @param outRoom Newly created Room.
     * @return EpicRtcErrorCode::Ok on success.
     */
    virtual EMRTC_API EpicRtcErrorCode CreateRoom(const EpicRtcRoomConfig& inConfig, EpicRtcRoomInterface** outRoom) = 0;

    /**
     * Removes Room object from this Session.
     * @param roomId Id of the room to remove.
     */
    virtual EMRTC_API void RemoveRoom(const EpicRtcStringView& roomId) = 0;

    // Prevent copying
    EpicRtcSessionInterface(const EpicRtcSessionInterface&) = delete;
    EpicRtcSessionInterface& operator=(const EpicRtcSessionInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcSessionInterface() = default;
    virtual EMRTC_API ~EpicRtcSessionInterface() = default;
};

#pragma pack(pop)
