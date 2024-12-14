// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "epic_rtc/common/defines.h"
#include "epic_rtc/containers/epic_rtc_string_view.h"

#include "epic_rtc/core/session_observer.h"

#pragma pack(push, 8)

/**
 * Session interface configuration object.
 */
struct EpicRtcSessionConfig
{
public:
    /**
     * Session id.
     */
    EpicRtcStringView _id;

    /**
     * Signalling server address.
     */
    EpicRtcStringView _url;

    EpicRtcSessionObserverInterface* _observer;
};

static_assert(sizeof(EpicRtcSessionConfig) == 16 + 16 + 8);  // Ensure is expected size on all platforms

#pragma pack(pop)
