// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "epic_rtc/common/defines.h"

#include <cstdint>

#pragma pack(push, 8)

/**
 * Interface to share objects over the library boundary by making them reference counted so that they are deleted inside the library.
 */
class EpicRtcRefCountInterface
{
public:
    /**
     * Increment reference count.
     * @return The reference count, used for testing only.
     */
    virtual EMRTC_API uint32_t AddRef() = 0;

    /**
     * Decrement Reference count and free if no references exist.
     * @return The reference count, used for testing only.
     */
    virtual EMRTC_API uint32_t Release() = 0;

    /**
     * Get reference Count. Use is only for testing purposes.
     * @return The reference count, used for testing only.
     */
    virtual EMRTC_API uint32_t Count() const = 0;

    // Prevent copying
    EpicRtcRefCountInterface(const EpicRtcRefCountInterface&) = delete;
    EpicRtcRefCountInterface& operator=(const EpicRtcRefCountInterface&) = delete;

protected:
    // Only class Implementation can be constructed or destroyed
    EMRTC_API EpicRtcRefCountInterface() = default;
    virtual EMRTC_API ~EpicRtcRefCountInterface() = default;
};

#pragma pack(pop)
