// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{

/**
 * Specifies a range using both beginning and ending indexes.
 * Often used with CUTF8StringView and SAxisRange (row and column)
 * CUTF8StringView can convert to and from SIdxRange and SAxisRange.
 **/
struct SIdxRange
{
public:

    // Public Data Members

    uint32_t _Begin;  ///< Beginning of index range
    uint32_t _End;    ///< End of index range

    // Construction

    ULANG_FORCEINLINE SIdxRange() : _Begin(0u), _End(0u)                                 {}
    ULANG_FORCEINLINE SIdxRange(ENoInit)                                                 {} // Do nothing - use with care!
    ULANG_FORCEINLINE SIdxRange(uint32_t Length) : _Begin(0u), _End(Length)              {}
    ULANG_FORCEINLINE SIdxRange(uint32_t Begin, uint32_t End) : _Begin(Begin), _End(End) {}
    ULANG_FORCEINLINE static SIdxRange MakeSpan(uint32_t Begin, uint32_t Length)         { return SIdxRange(Begin, Begin + Length); }
    ULANG_FORCEINLINE void Reset()                                                       { _Begin = _End = 0u; }

    // Accessors

    ULANG_FORCEINLINE uint32_t GetLength() const                { return _End - _Begin; }
    ULANG_FORCEINLINE bool IsEmpty() const                      { return _Begin == _End; }
    ULANG_FORCEINLINE bool IsOrdered() const                    { return _Begin <= _End; }
    ULANG_FORCEINLINE void Set(uint32_t Begin, uint32_t End)    { _Begin = Begin; _End = End; }
    ULANG_FORCEINLINE void AdvanceToEnd()                       { _Begin = _End; }

    // Comparisons

    ULANG_FORCEINLINE bool operator==(const SIdxRange & Other) const  { return ((_Begin == Other._Begin) && (_End == Other._End)); }
    ULANG_FORCEINLINE bool operator!=(const SIdxRange & Other) const  { return ((_Begin != Other._Begin) || (_End != Other._End)); }

};  // SIdxRange

}