// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/common.h"

#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/core/audio/audio_frame.h"

#pragma pack(push, 8)

class EpicRtcAudioTrackInterface;  // forward declaration

class EpicRtcAudioTrackObserverInterface : public EpicRtcRefCountInterface
{
public:
    virtual void OnAudioTrackMuted(EpicRtcAudioTrackInterface*, EpicRtcBool mute) = 0;
    virtual void OnAudioTrackFrame(EpicRtcAudioTrackInterface*, const EpicRtcAudioFrame&) = 0;
    virtual void OnAudioTrackRemoved(EpicRtcAudioTrackInterface*) = 0;
    virtual void OnAudioTrackState(EpicRtcAudioTrackInterface*, const EpicRtcTrackState) = 0;
};

class EpicRtcAudioTrackObserverFactoryInterface : public EpicRtcRefCountInterface
{
public:
    virtual EpicRtcErrorCode CreateAudioTrackObserver(const EpicRtcStringView participantId, const EpicRtcStringView audioTrackId, EpicRtcAudioTrackObserverInterface** outAudioTrackObserver) = 0;
};

#pragma pack(pop)
