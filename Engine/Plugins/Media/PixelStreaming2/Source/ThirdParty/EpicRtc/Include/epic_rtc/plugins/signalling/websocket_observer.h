// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/containers/epic_rtc_string_view.h"

#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

class EpicRtcBaseWebsocketObserverInterface
{
public:
    virtual ~EpicRtcBaseWebsocketObserverInterface() = default;
    virtual void OnOpen() = 0;
    virtual void OnClosed() = 0;
    virtual void OnConnectionError(EpicRtcStringView errorMessage) = 0;
    virtual void OnMessage(EpicRtcStringView message) = 0;
};

class EpicRtcWebsocketObserverInterface : public EpicRtcBaseWebsocketObserverInterface, public EpicRtcRefCountInterface
{
public:
    virtual ~EpicRtcWebsocketObserverInterface() = default;
};

#pragma pack(pop)
