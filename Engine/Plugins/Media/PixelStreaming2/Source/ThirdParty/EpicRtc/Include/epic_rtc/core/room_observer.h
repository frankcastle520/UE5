// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/participant.h"
#include "epic_rtc/core/audio/audio_track.h"
#include "epic_rtc/core/data_track.h"
#include "epic_rtc/core/participant.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/sdp.h"
#include "epic_rtc/core/video/video_track.h"

#pragma pack(push, 8)
class EpicRtcRoomObserverInterface : public EpicRtcRefCountInterface
{
public:
    virtual ~EpicRtcRoomObserverInterface() = default;

    virtual void OnRoomStateUpdate(const EpicRtcRoomState inState) = 0;

    virtual void OnRoomJoinedUpdate(EpicRtcParticipantInterface* inParticipant) = 0;

    virtual void OnRoomLeftUpdate(const EpicRtcStringView inParticipantId) = 0;

    virtual void OnAudioTrackUpdate(EpicRtcParticipantInterface* inParticipant, EpicRtcAudioTrackInterface* inAudioTrack) = 0;

    virtual void OnVideoTrackUpdate(EpicRtcParticipantInterface* inParticipant, EpicRtcVideoTrackInterface* inVideoTrack) = 0;

    virtual void OnDataTrackUpdate(EpicRtcParticipantInterface* inParticipant, EpicRtcDataTrackInterface* inDataTrack) = 0;

    [[nodiscard]] virtual EpicRtcSdpInterface* OnLocalSdpUpdate(EpicRtcParticipantInterface* inParticipant, EpicRtcSdpInterface* inOutSdp) = 0;

    [[nodiscard]] virtual EpicRtcSdpInterface* OnRemoteSdpUpdate(EpicRtcParticipantInterface* inParticipant, EpicRtcSdpInterface* inOutSdp) = 0;

    virtual void OnRoomErrorUpdate(const EpicRtcErrorCode inError) = 0;
};

#pragma pack(pop)
