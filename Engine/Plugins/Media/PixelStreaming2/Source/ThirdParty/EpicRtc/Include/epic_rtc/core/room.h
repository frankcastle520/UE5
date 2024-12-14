// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/containers/epic_rtc_string.h"
#include "epic_rtc/common/defines.h"
#include "epic_rtc/common/common.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/connection.h"

#pragma pack(push, 8)

/**
 * Represents the media room (conference) with its state and participants.
 */
class EpicRtcRoomInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets instance Id
     * @return Id
     */
    virtual EMRTC_API EpicRtcStringView GetId() = 0;

    /**
     * Get the Room mode.
     * @return Mode this Room operates in.
     */
    virtual EMRTC_API EpicRtcRoomMode GetMode() = 0;

    /**
     * Joins the Room.
     */
    virtual EMRTC_API void Join() = 0;

    /**
     * Leaves the Room
     */
    virtual EMRTC_API void Leave() = 0;

    /**
     * This is a temporary method to get the global connection in case we are running the MediaServer mode.
     * @return MediaServer EpicRtcConnectionInterface
     */
    virtual EMRTC_API EpicRtcErrorCode GetMediaServerConnection(EpicRtcConnectionInterface** outMediaServerConnection) = 0;

    // Prevent copying
    EpicRtcRoomInterface(const EpicRtcRoomInterface&) = delete;
    EpicRtcRoomInterface& operator=(const EpicRtcRoomInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcRoomInterface() = default;
    virtual EMRTC_API ~EpicRtcRoomInterface() = default;
};

#pragma pack(pop)
