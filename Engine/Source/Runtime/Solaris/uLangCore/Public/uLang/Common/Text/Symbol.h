// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/SharedPointer.h" // For CSharedMix
#include "uLang/Common/Memory/ArenaAllocator.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/UTF8String.h"

namespace uLang
{

/// A unique id representing a symbol
using SymbolId = uint32_t;

/// The id of the empty string, hardcoded to a fixed value
enum { SymbolId_Null = 0 };

class CSymbol;

/// Database keeping track of symbols and their text equivalent
class ULANGCORE_API CSymbolTable : public CSharedMix
{
public:
    /// @param NumHashBuckets How many hash buckets are used for lookup by text (use 0 to disable text lookup)
    /// @param IdChunkShift   2^IdChunkShift pointers are stored in a chunk for lookup by id (use 0 to disable id lookup)
    CSymbolTable(uint32_t NumHashBuckets = 1024, uint32_t IdChunkShift = 10);

    ~CSymbolTable();

    /// Gets a symbol by id - id must exist
    CSymbol Get(SymbolId Id) const;

    /// Looks up a symbol by text
    TOptional<CSymbol> Find(const CUTF8StringView& Text, bool bIsGenerated = false) const;

    /// Looks up a symbol by text, and if not present yet, adds it. Return empty optional if text is to long.
    TOptional<CSymbol> Add(const CUTF8StringView& Text, bool bIsGenerated = false);

    /// Looks up a symbol by text, and if not present yet, adds it. Asserts if text is to long.
    CSymbol AddChecked(const CUTF8StringView& Text, bool bIsGenerated = false);

    /// Remaps a symbol from another symbol table to this symbol table
    /// I.e. symbol tables are merged by iterating through all symbols of one table and re-adding them to the other table
    void ReAdd(CSymbol& Symbol);

    /// The max length of a symbol.
    static constexpr uint32_t MaxSymbolLength = 1024; // A symbol can be accepted here, but fail later in the compiler due to name transformations.   

private:
    friend class CSymbol;

    /// Entry in the symbol table that stores the id and string
    /// The string is stored in the memory following this data structure
    struct SEntry
    {
        SEntry*     _HashNext;         ///< Next entry with the same hash
        SymbolId    _Id;               ///< The ID of this symbol
        uint32_t    _ByteLength : 31;  ///< Length of text string in bytes
        uint32_t    _bIsGenerated : 1; ///< Whether the symbol is generated.
        UTF8Char    _Data[1];

        ULANG_FORCEINLINE CUTF8StringView AsStringView() const
        {
            return { _Data, _Data + _ByteLength };
        }

        ULANG_FORCEINLINE const char* AsCString() const
        {
            return (const char *)_Data;
        }
    };

    uint32_t       GetBucketIndex(const CUTF8StringView& Text) const;
    const SEntry * AddInternal(const CUTF8StringView& Text, uint32_t BucketIndex, bool bIsGenerated);
    const SEntry * FindOrAddInternal(const CUTF8StringView& Text, bool bIsGenerated);

    // No copying allowed
    CSymbolTable(const CSymbolTable&) = delete;
    void operator = (const CSymbolTable&) = delete;
    void operator = (CSymbolTable&&) = delete;

    TArray<SEntry*>  _HashBuckets;    ///< The heads of the lists of each string hash bucket
    CArenaAllocator  _Allocator;      ///< For allocating entries
    TArray<SEntry**> _IdLookupTable;  ///< Array of chunks for lookup by id
    uint32_t         _IdChunkShift;   ///< 2^_IdLookupChunkShift pointers are stored per chunk
    SymbolId         _HighestUsedId;  ///< Highest id used so far in this table

    static const SEntry _EntryNull; ///< Entry representing the null symbol (= empty string)
};

/// Symbol representing a text string with an associated id
class CSymbol
{
public:
    ULANG_FORCEINLINE CSymbol() : _Entry(&CSymbolTable::_EntryNull) {}

    ULANG_FORCEINLINE SymbolId GetId() const   { return _Entry->_Id; }
    ULANG_FORCEINLINE bool IsNull() const      { return _Entry->_Id == SymbolId_Null; }
    ULANG_FORCEINLINE bool IsGenerated() const { return _Entry->_bIsGenerated; }

    ULANG_FORCEINLINE CUTF8String AsString() const          { return _Entry->AsStringView(); }
    ULANG_FORCEINLINE CUTF8StringView AsStringView() const  { return _Entry->AsStringView(); }
    ULANG_FORCEINLINE const char* AsCString() const         { return _Entry->AsCString(); }
    ULANG_FORCEINLINE UTF8Char FirstByte() const            { return _Entry->_Data[0]; }

    ULANG_FORCEINLINE EEquate Compare(const CSymbol& Other) const   { return (_Entry == Other._Entry) ? EEquate::Equal : ((_Entry < Other._Entry) ? EEquate::Less : EEquate::Greater); }
    ULANG_FORCEINLINE bool operator == (const CSymbol& Other) const { return _Entry == Other._Entry; }
    ULANG_FORCEINLINE bool operator != (const CSymbol& Other) const { return _Entry != Other._Entry; }
    ULANG_FORCEINLINE bool operator <= (const CSymbol& Other) const { return _Entry <= Other._Entry; }
    ULANG_FORCEINLINE bool operator >= (const CSymbol& Other) const { return _Entry >= Other._Entry; }
    ULANG_FORCEINLINE bool operator <  (const CSymbol& Other) const { return _Entry <  Other._Entry; }
    ULANG_FORCEINLINE bool operator >  (const CSymbol& Other) const { return _Entry >  Other._Entry; }
    
    /// Hash function for maps, sets
    ULANG_FORCEINLINE friend uint32_t GetTypeHash(CSymbol Symbol)
    {
        return Symbol._Entry->_Id;
    }

private:
    friend class CSymbolTable;

    ULANG_FORCEINLINE CSymbol(const CSymbolTable::SEntry* Entry) : _Entry(Entry) {}

    const CSymbolTable::SEntry * _Entry;
};

//=======================================================================================
// CSymbolTable Inline Methods
//=======================================================================================

ULANG_FORCEINLINE CSymbol CSymbolTable::Get(SymbolId Id) const
{
    ULANG_ASSERTF(Id <= _HighestUsedId, "Id out of range!");

    if (Id == SymbolId_Null) return CSymbol();

    uint32_t Index = Id - 1;
    uint32_t ChunkIndex = Index >> _IdChunkShift;
    uint32_t WithinChunkIndex = Index & ((1 << _IdChunkShift) - 1);
    return CSymbol(_IdLookupTable[ChunkIndex][WithinChunkIndex]);
}

}