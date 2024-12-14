// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "websocket_observer.h"

#include "epic_rtc/common/common.h"
#include "epic_rtc/core/ref_count.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"

#pragma pack(push, 8)

class EpicRtcWebsocketInterface : public EpicRtcRefCountInterface
{
public:
    virtual EpicRtcBool Connect(EpicRtcStringView url, EpicRtcWebsocketObserverInterface* observer) = 0;
    virtual void Disconnect(const EpicRtcStringView reason) = 0;
    virtual void Send(EpicRtcStringView message) = 0;
};

#pragma pack(pop)
