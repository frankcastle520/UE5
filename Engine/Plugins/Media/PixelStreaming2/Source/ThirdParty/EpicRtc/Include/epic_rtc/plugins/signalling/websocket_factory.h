// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "websocket.h"

#include "epic_rtc/common/common.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

class EpicRtcWebsocketFactoryInterface : public EpicRtcRefCountInterface
{
public:
    virtual EpicRtcErrorCode CreateWebsocket(EpicRtcWebsocketInterface** outWebsocket) = 0;
};

#pragma pack(pop)
