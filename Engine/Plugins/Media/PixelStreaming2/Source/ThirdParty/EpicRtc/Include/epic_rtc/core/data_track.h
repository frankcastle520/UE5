// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>
#include <cassert>

#include "epic_rtc/common/common.h"
#include "epic_rtc/common/defines.h"

#include "epic_rtc/containers/epic_rtc_string_view.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

struct EpicRtcDataFrameInput
{
    /**
     * Data buffer
     * User of the API has ownership of this data and should free once done
     */
    uint8_t* _data;

    /**
     * Size of the data buffer
     */
    uint32_t _size;

    /**
     * Indicates this is a binary data frame (not a string)
     */
    EpicRtcBool _binary;
};

static_assert(sizeof(EpicRtcDataFrameInput) == 16);  // Ensure EpicRtcDataFrameInput is expected size on all platforms

class EpicRtcDataFrameInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Data buffer
     */
    virtual const uint8_t* Data() const = 0;

    /**
     * Size of the data buffer
     */
    virtual uint32_t Size() const = 0;

    /**
     * Indicates this is a binary data frame (not a string)
     */
    virtual EpicRtcBool IsBinary() const = 0;

    // Prevent copying
    EpicRtcDataFrameInterface(const EpicRtcDataFrameInterface&) = delete;
    EpicRtcDataFrameInterface& operator=(const EpicRtcDataFrameInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcDataFrameInterface() = default;
    virtual EMRTC_API ~EpicRtcDataFrameInterface() = default;
};

/**
 * Represents the data track. Exposes methods to send and receive custom data.
 */
class EpicRtcDataTrackInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Gets instance Id.
     * @return Id.
     */
    virtual EMRTC_API EpicRtcStringView GetId() = 0;

    /**
     * Gets instance label.
     * @return Label.
     */
    virtual EMRTC_API EpicRtcStringView GetLabel() = 0;

    /**
     * Pop frame for processing. The user must call release on the frame else it will leak.
     * @param outFrame next available frame. The user must call release on the frame.
     * @return False if no frame was available or failed to pop the frame
     */
    virtual EMRTC_API EpicRtcBool PopFrame(EpicRtcDataFrameInterface** outFrame) = 0;

    /**
     * Supply frame for processing.
     * @param InFrame Frame to process.
     * @return False if error pushing frame.
     */
    virtual EMRTC_API EpicRtcBool PushFrame(const EpicRtcDataFrameInput& inFrame) = 0;

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

    // Prevent copying
    EpicRtcDataTrackInterface(const EpicRtcDataTrackInterface&) = delete;
    EpicRtcDataTrackInterface& operator=(const EpicRtcDataTrackInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcDataTrackInterface() = default;
    virtual EMRTC_API ~EpicRtcDataTrackInterface() = default;
};

#pragma pack(pop)
