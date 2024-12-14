// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

#include "epic_rtc/containers/epic_rtc_string_view.h"

#pragma pack(push, 8)

enum class EpicRtcLogLevel : uint8_t
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5,
    Off = 6,
};

struct EpicRtcLogMessage
{
    /*
     * Name of the module generated the log. E.g. EpicRtc, WebRTC.
     */
    EpicRtcStringView _moduleName;

    /*
     * Log message.
     */
    EpicRtcStringView _message;

    /*
     * Log level.
     */
    EpicRtcLogLevel _level;

    /*
     * Path to source file.
     */
    EpicRtcStringView _fileName;

    /*
     * Line number.
     */
    uint32_t _line;
};

static_assert(sizeof(EpicRtcLogMessage) == 64);  // Ensure EpicRtcLogMessage is expected size on all platforms

class EpicRtcLoggerInterface
{
public:
    virtual ~EpicRtcLoggerInterface() = default;

    virtual void Log(const EpicRtcLogMessage& message) = 0;
};

#pragma pack(pop)
