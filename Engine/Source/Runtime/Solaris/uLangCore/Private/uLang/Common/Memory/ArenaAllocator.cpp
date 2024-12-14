// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Memory/ArenaAllocator.h"

namespace uLang
{

CArenaAllocator::CArenaAllocator(uint32_t ArenaSize)
    : CAllocatorInstance(Allocate, Reallocate, Deallocate)
    , _First(nullptr)
    , _ArenaSize(ArenaSize)
    , _BytesLeftInFirstArena(0)
#if ULANG_DO_CHECK
    , _NumAllocations(0)
    , _MatchDeallocations(false)
    , _NumArenas(0)
    , _BytesAllocatedTotal(0)
#endif
{
}

CArenaAllocator::~CArenaAllocator()
{
    DeallocateAll();
}

void CArenaAllocator::Merge(CArenaAllocator && Other)
{
    // Find tail and append other list of arenas
    SArenaHeader ** Tail = &_First;
    while (*Tail)
    {
        Tail = &(*Tail)->_Next;
    }
    *Tail = Other._First;

    // Reset other header
    Other._First = nullptr;
    Other._BytesLeftInFirstArena = 0;

#if ULANG_DO_CHECK
    _NumAllocations += Other._NumAllocations;
    _NumArenas += Other._NumArenas;
    _BytesAllocatedTotal += Other._BytesAllocatedTotal;
    Other._NumAllocations = 0;
    Other._NumArenas = 0;
    Other._BytesAllocatedTotal = 0;
#endif
}

void * CArenaAllocator::Allocate(const CAllocatorInstance * This, size_t NumBytes)
{
    CArenaAllocator * Allocator = const_cast<CArenaAllocator *>(static_cast<const CArenaAllocator *>(This));
    return Allocator->Allocate(uint32_t(NumBytes));
}

void CArenaAllocator::DeallocateAll()
{
#if ULANG_DO_CHECK
    ULANG_VERIFYF(!_MatchDeallocations || _NumAllocations == 0, "CArenaAllocator: Number of allocations and deallocations don't match!");
#endif

    // Loop over arenas and free their memory
    SArenaHeader * NextArena;
    for (SArenaHeader * Arena = _First; Arena; Arena = NextArena)
    {
        NextArena = Arena->_Next;
        GetSystemParams()._HeapFree(Arena);
    }
    // Reset header
    _First = nullptr;
    _BytesLeftInFirstArena = 0;

#if ULANG_DO_CHECK
    _NumAllocations = 0;
    _MatchDeallocations = false;
    _NumArenas = 0;
    _BytesAllocatedTotal = 0;
#endif
}

void * CArenaAllocator::Reallocate(const CAllocatorInstance * This, void * Memory, size_t NumBytes)
{
    // Reallocation is a BAD idea with an arena allocator, so disallow it even though technically possible
    ULANG_ERRORF("Must not reallocate from an arena allocator!");
    return nullptr;
}

void CArenaAllocator::Deallocate(const CAllocatorInstance * This, void * Memory)
{
    // Deallocation is really not supported by arena allocation, so just no nothing except checking consistency

#if ULANG_DO_CHECK
    CArenaAllocator * Allocator = const_cast<CArenaAllocator *>(static_cast<const CArenaAllocator *>(This));
    --Allocator->_NumAllocations;
    Allocator->_MatchDeallocations = true; // Once a single deallocation occurs, the matching check is enabled
#endif
}

void CArenaAllocator::AllocateNewArena()
{
    SArenaHeader * NewArena = (SArenaHeader *)GetSystemParams()._HeapMalloc(sizeof(SArenaHeader) + _ArenaSize);
    NewArena->_Next = _First;
    _First = NewArena;
    _BytesLeftInFirstArena = _ArenaSize;

#if ULANG_DO_CHECK
    ++_NumArenas;
#endif
}

};


