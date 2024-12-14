// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Common.h"
#include "uLang/Common/Memory/Allocator.h"

namespace uLang
{

SSystemParams& GetSystemParams()
{
    static SSystemParams Instance = { 0, nullptr, nullptr, nullptr, nullptr, nullptr };
    return Instance;
}

const CAllocatorInstance GSystemAllocatorInstance(
    [](const CAllocatorInstance *, size_t NumBytes) -> void * { return GetSystemParams()._HeapMalloc(NumBytes); },
    [](const CAllocatorInstance *, void * Memory, size_t NumBytes) -> void * { return GetSystemParams()._HeapRealloc(Memory, NumBytes); },
    [](const CAllocatorInstance *, void * Memory) { GetSystemParams()._HeapFree(Memory); }
);

bool operator==(const SSystemParams& Lhs, const SSystemParams& Rhs)
{
    return Lhs._APIVersion == Rhs._APIVersion &&
        Lhs._HeapMalloc == Rhs._HeapMalloc &&
        Lhs._HeapFree == Rhs._HeapFree &&
        Lhs._HeapRealloc == Rhs._HeapRealloc &&
        Lhs._AssertFailed == Rhs._AssertFailed;
}

EResult Initialize(const SSystemParams & Params)
{
    GetSystemParams() = Params;

    ULANG_ASSERTF(GetSystemParams()._APIVersion == ULANG_API_VERSION, "Version mismatch (expected %d, got %d)! Are you linking with a stale DLL?", int32_t(ULANG_API_VERSION), GetSystemParams()._APIVersion);

    return EResult::OK;
}

bool IsInitialized()
{
    return GetSystemParams()._APIVersion != 0;
}

EResult DeInitialize()
{
    return EResult::OK;
}

void SetGlobalVerbosity(const uLang::ELogVerbosity GlobalVerbosity)
{
    GetSystemParams()._Verbosity = GlobalVerbosity;
}

}