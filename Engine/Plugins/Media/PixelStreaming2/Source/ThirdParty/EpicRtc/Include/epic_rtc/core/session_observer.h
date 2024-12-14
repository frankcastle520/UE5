// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/common.h"
#include "epic_rtc/containers/epic_rtc_array.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)
class EpicRtcSessionObserverInterface : public EpicRtcRefCountInterface
{
public:
    virtual ~EpicRtcSessionObserverInterface() = default;

    virtual void OnSessionStateUpdate(const EpicRtcSessionState inState) = 0;

    virtual void OnSessionErrorUpdate(const EpicRtcErrorCode inErrorCode) = 0;

    virtual void OnSessionRoomsAvailableUpdate(EpicRtcStringArrayInterface* roomsList) = 0;
};
#pragma pack(pop)
