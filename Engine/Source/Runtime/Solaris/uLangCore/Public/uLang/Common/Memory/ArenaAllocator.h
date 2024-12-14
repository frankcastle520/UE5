// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Memory/Allocator.h"
#include "uLang/Common/Templates/Storage.h"

namespace uLang
{

/// Allocates from a series of arenas
/// Memory blocks can not be individually deallocated
/// Deleting this allocator will free all allocated memory
/// NOT thread safe (on purpose, for efficiency)
class ULANGCORE_API CArenaAllocator : public CAllocatorInstance
{
public:

    CArenaAllocator(uint32_t ArenaSize);
    ~CArenaAllocator();

    void   Merge(CArenaAllocator && Other);

    void * Allocate(uint32_t NumBytes);
    void   DeallocateAll();

protected:

    enum { Alignment = 8 }; ///< Alignment is hardcoded to 8 for now

    static void * Allocate(const CAllocatorInstance * This, size_t NumBytes);
    static void * Reallocate(const CAllocatorInstance * This, void * Memory, size_t NumBytes);
    static void   Deallocate(const CAllocatorInstance * This, void * Memory);

    // Get more memory
    void AllocateNewArena();

    /// Header of an arena, the allocated memory follows
    struct SArenaHeader
    {
        SArenaHeader * _Next;
    };

    SArenaHeader *  _First;        ///< The first in the list of arenas
    const uint32_t  _ArenaSize;    ///< Memory in each arena, in bytes
    uint32_t        _BytesLeftInFirstArena; ///< How much memory is available in the first arena

#if ULANG_DO_CHECK
    uint32_t        _NumAllocations;
    bool            _MatchDeallocations;
    uint32_t        _NumArenas;
    uint32_t        _BytesAllocatedTotal;
#endif
};

//=======================================================================================
// CArenaAllocator Inline Methods
//=======================================================================================

ULANG_FORCEINLINE void * CArenaAllocator::Allocate(uint32_t NumBytes)
{
    NumBytes = AlignUp(NumBytes, Alignment);

#if ULANG_DO_CHECK
    ULANG_ASSERTF(NumBytes <= _ArenaSize, "Must not allocate a memory block larger than the arena size!");
#endif

    if (NumBytes > _BytesLeftInFirstArena)
    {
        AllocateNewArena();
    }

    uint8_t * Memory = ((uint8_t *)(_First + 1)) + (_ArenaSize - _BytesLeftInFirstArena);
    _BytesLeftInFirstArena -= NumBytes;

#if ULANG_DO_CHECK
    ++_NumAllocations;
    _BytesAllocatedTotal += NumBytes;
#endif

    return Memory;
}

}