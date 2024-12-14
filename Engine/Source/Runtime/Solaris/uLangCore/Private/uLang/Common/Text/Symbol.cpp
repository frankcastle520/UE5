// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Memory/ArenaAllocator.h"
#include "uLang/Common/Misc/CRC.h"
#include <cstddef>

namespace uLang
{

const CSymbolTable::SEntry CSymbolTable::_EntryNull = { nullptr, SymbolId_Null, 0, false, {0} };

CSymbolTable::CSymbolTable(uint32_t NumHashBuckets, uint32_t IdChunkShift)
    : _Allocator(4096)
    , _IdChunkShift(IdChunkShift)
    , _HighestUsedId(SymbolId_Null)
{
    ULANG_ASSERTF(CMath::IsPowerOf2(NumHashBuckets), "NumHashBuckets must be a power of 2!");

    _HashBuckets.AddZeroed(NumHashBuckets);
    if (_IdChunkShift)
    {
        // We're eventually going to allocate chunks anyway so give ourselves a bit of runway before we need to reallocate
        // Chunks are just pointers so this really is a minuscule amount of memory
        _IdLookupTable.Reserve(32);
    }
}

CSymbolTable::~CSymbolTable()
{
    // Free id lookup chunks
    for (SEntry ** Chunk : _IdLookupTable)
    {
        GetSystemParams()._HeapFree(Chunk);
    }
}

ULANG_FORCEINLINE uint32_t CSymbolTable::GetBucketIndex(const CUTF8StringView & Text) const
{
    return CCRC16::Generate(Text._Begin, Text._End) & (_HashBuckets.Num() - 1);
}

ULANG_FORCEINLINE const CSymbolTable::SEntry * CSymbolTable::AddInternal(const CUTF8StringView & Text, uint32_t BucketIndex, bool bIsGenerated)
{
    // Allocate a new entry
    const uint32_t ByteLength = Text.ByteLen();
    const size_t EntryTotalBytes = offsetof(SEntry, _Data) + ByteLength + 1;
    if (ByteLength > MaxSymbolLength)
    {
        return nullptr;
    }
    SEntry * NewEntry = (SEntry *)_Allocator.Allocate(uint32_t(EntryTotalBytes));
    SymbolId NewId = ++_HighestUsedId;
    NewEntry->_ByteLength = ByteLength;
    NewEntry->_bIsGenerated = bIsGenerated;
    NewEntry->_Id = NewId;

    // Copy string into new entry
    memcpy(NewEntry->_Data, Text._Begin, ByteLength);
    NewEntry->_Data[ByteLength] = 0; // Add null terminator

    // Add to hash bucket
    if (_HashBuckets.Num())
    {
        SEntry *& HeadEntry = _HashBuckets[BucketIndex];
        NewEntry->_HashNext = HeadEntry;
        HeadEntry = NewEntry;
    }

    // Also add to id lookup table
    if (_IdChunkShift)
    {
        uint32_t Index = NewId - 1;
        uint32_t ChunkSize = 1 << _IdChunkShift;
        uint32_t ChunkIndex = Index >> _IdChunkShift;
        uint32_t WithinChunkIndex = Index & (ChunkSize - 1);
        if (WithinChunkIndex == 0)
        {
            // Add new chunk
            SEntry ** NewChunk = (SEntry **)GetSystemParams()._HeapMalloc(ChunkSize * sizeof(SEntry*));
            _IdLookupTable.Add(NewChunk);
            ULANG_ASSERTF(((uint32_t)_IdLookupTable.Num()) == ChunkIndex + 1, "Must add exactly to the end of the lookup table.");
        }
        _IdLookupTable[ChunkIndex][WithinChunkIndex] = NewEntry;
    }

    return NewEntry;
}

ULANG_FORCEINLINE const CSymbolTable::SEntry * CSymbolTable::FindOrAddInternal(const CUTF8StringView & Text, bool bIsGenerated)
{
    // The null symbol corresponds to the empty string
    if (Text.IsEmpty())
    {
        return &CSymbolTable::_EntryNull;
    }

    // Otherwise look it up in the table
    uint32_t BucketIndex = GetBucketIndex(Text);
    for (SEntry * Entry = _HashBuckets[BucketIndex]; Entry; Entry = Entry->_HashNext)
    {
        if (Entry->AsStringView() == Text && !!Entry->_bIsGenerated == bIsGenerated) return Entry;
    }

    return AddInternal(Text, BucketIndex, bIsGenerated);
}

TOptional<CSymbol> CSymbolTable::Find(const CUTF8StringView & Text, bool bIsGenerated) const
{
    // The null symbol corresponds to the empty string
    if (Text.IsEmpty())
    {
        return CSymbol(&CSymbolTable::_EntryNull);
    }

    // Otherwise look it up in the table
    uint32_t BucketIndex = GetBucketIndex(Text);
    for (SEntry * Entry = _HashBuckets[BucketIndex]; Entry; Entry = Entry->_HashNext)
    {
        if (Entry->AsStringView() == Text && !!Entry->_bIsGenerated == bIsGenerated) return CSymbol(Entry);
    }

    return TOptional<CSymbol>();
}

TOptional<CSymbol> CSymbolTable::Add(const CUTF8StringView & Text, bool bIsGenerated)
{
    if (const CSymbolTable::SEntry* Symbol = FindOrAddInternal(Text, bIsGenerated))
    {
        return CSymbol(Symbol);
    }
    return TOptional<CSymbol>();
}

CSymbol CSymbolTable::AddChecked(const CUTF8StringView& Text, bool bIsGenerated)
{
    const CSymbolTable::SEntry* Symbol = FindOrAddInternal(Text, bIsGenerated);
    ULANG_ASSERTF(Symbol != nullptr, "Identifier is to long.");
    return CSymbol(Symbol);
}
 
void CSymbolTable::ReAdd(CSymbol & Symbol)
{
    if (Symbol._Entry != &CSymbolTable::_EntryNull)
    {
        Symbol._Entry = FindOrAddInternal(Symbol.AsStringView(), Symbol.IsGenerated());
    }
}

};
