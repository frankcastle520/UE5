// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/common.h"

#include "epic_rtc/containers/epic_rtc_string_view.h"

#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

class EpicRtcDataTrackInterface;  // forward declaration

class EpicRtcDataTrackObserverInterface : public EpicRtcRefCountInterface
{
public:
    virtual void OnDataTrackState(EpicRtcDataTrackInterface*, const EpicRtcTrackState) = 0;
    virtual void OnDataTrackMessage(EpicRtcDataTrackInterface*) = 0;
    virtual void OnDataTrackError(EpicRtcDataTrackInterface*, const EpicRtcErrorCode) = 0;
};

class EpicRtcDataTrackObserverFactoryInterface : public EpicRtcRefCountInterface
{
public:
    virtual EpicRtcErrorCode CreateDataTrackObserver(const EpicRtcStringView participantId, const EpicRtcStringView dataTrackId, EpicRtcDataTrackObserverInterface** outDataTrackObserver) = 0;
};

#pragma pack(pop)
