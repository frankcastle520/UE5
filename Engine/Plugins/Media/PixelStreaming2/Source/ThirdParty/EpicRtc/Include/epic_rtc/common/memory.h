// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <cstdint>

#pragma pack(push, 8)

class EpicRtcMemoryInterface
{
public:
    [[nodiscard]] virtual void* Allocate(uint64_t size, uint64_t alignment, const char* tag) = 0;
    [[nodiscard]] virtual void* Reallocate(void* pointer, uint64_t size, uint64_t alignment, const char* tag) = 0;
    virtual void Free(void* pointer) = 0;
    virtual ~EpicRtcMemoryInterface() = default;
};

#pragma pack(pop)
