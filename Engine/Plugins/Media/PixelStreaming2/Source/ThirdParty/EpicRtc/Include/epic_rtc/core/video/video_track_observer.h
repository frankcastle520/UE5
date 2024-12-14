// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/common.h"

#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/video/video_frame.h"

#pragma pack(push, 8)

class EpicRtcVideoTrackInterface;  // forward declaration

class EpicRtcVideoTrackObserverInterface : public EpicRtcRefCountInterface
{
public:
    virtual void OnVideoTrackMuted(EpicRtcVideoTrackInterface*, EpicRtcBool mute) = 0;
    virtual void OnVideoTrackRemoved(EpicRtcVideoTrackInterface*) = 0;
    virtual void OnVideoTrackState(EpicRtcVideoTrackInterface*, const EpicRtcTrackState) = 0;
    virtual void OnVideoTrackFrame(EpicRtcVideoTrackInterface*, const EpicRtcVideoFrame&) = 0;
    virtual void OnVideoTrackEncodedFrame(EpicRtcVideoTrackInterface*, const EpicRtcEncodedVideoFrame&) = 0;

    /**
     * Indicates whether the observer is ready to receive messages.
     * If false, any method calls will be ignored.
     * @return EpicRtcBool Observer enabled state
     */
    virtual EpicRtcBool Enabled() const = 0;
};

class EpicRtcVideoTrackObserverFactoryInterface : public EpicRtcRefCountInterface
{
public:
    virtual EpicRtcErrorCode CreateVideoTrackObserver(const EpicRtcStringView participantId, const EpicRtcStringView videoTrackId, EpicRtcVideoTrackObserverInterface** outVideoTrackObserver) = 0;
};

#pragma pack(pop)
