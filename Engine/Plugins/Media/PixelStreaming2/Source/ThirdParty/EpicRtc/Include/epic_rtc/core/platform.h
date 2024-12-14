// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

#include "epic_rtc/common/memory.h"
#include "epic_rtc/core/conference.h"
#include "epic_rtc/core/conference_config.h"

#pragma pack(push, 8)
class EpicRtcPlatformInterface : public EpicRtcRefCountInterface
{
public:
    EpicRtcPlatformInterface(const EpicRtcPlatformInterface&) = delete;
    EpicRtcPlatformInterface& operator=(const EpicRtcPlatformInterface&) = delete;

    virtual EMRTC_API EpicRtcErrorCode CreateConference(EpicRtcStringView inId, const EpicRtcConfig& inConfig, EpicRtcConferenceInterface** outConference) = 0;
    virtual EMRTC_API EpicRtcErrorCode GetConference(EpicRtcStringView inId, EpicRtcConferenceInterface** outConference) const = 0;
    virtual EMRTC_API void ReleaseConference(EpicRtcStringView inId) = 0;

protected:
    EMRTC_API EpicRtcPlatformInterface() = default;
    virtual EMRTC_API ~EpicRtcPlatformInterface() = default;
};

struct EpicRtcPlatformConfig
{
    EpicRtcMemoryInterface* _memory;
};

static_assert(sizeof(EpicRtcPlatformConfig) == 8);  // Ensure EpicRtcPlatformConfig is expected size on all platforms

// Global function for accessing EpicRtcPlatformInferface
extern "C" EMRTC_API EpicRtcErrorCode GetOrCreatePlatform(const EpicRtcPlatformConfig& inConfig, EpicRtcPlatformInterface** outPlatform);

#pragma pack(pop)
