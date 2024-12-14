// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

#include "epic_rtc/common/defines.h"
#include "epic_rtc/core/ref_count.h"

#pragma pack(push, 8)

/**
 * A reference counted String container that allows strings to be copied over ABI and DLL boundary.
 * Normally a std::string may not be valid when passed between DLLs and memory allocation may be different.
 */
class EpicRtcStringInterface : public EpicRtcRefCountInterface
{
public:
    /**
     * Get the pointer to the string.
     * @return The string.
     */
    virtual EMRTC_API const char* Get() const = 0;

    /**
     * Get the length  of the string in number of characters.
     * @return the length of the string.
     */
    virtual EMRTC_API uint64_t Length() const = 0;

    // Prevent copying
    EpicRtcStringInterface(const EpicRtcStringInterface&) = delete;
    EpicRtcStringInterface& operator=(const EpicRtcStringInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcStringInterface() = default;
    virtual EMRTC_API ~EpicRtcStringInterface() = default;
};

#pragma pack(pop)
