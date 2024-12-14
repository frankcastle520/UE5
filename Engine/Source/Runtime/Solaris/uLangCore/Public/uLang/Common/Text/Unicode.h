// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{
// UTF-8 Context
//   - http://utf8everywhere.org/
//   - https://github.com/nemtrif/utfcpp

// ASCII Character range is from 0 to 127 so any byte less than this is a valid ASCII character.
inline constexpr size_t ASCII_RANGE = 128;
inline constexpr size_t BYTE_RANGE = 256;

/// UTF-8 octet
using UTF8Char = uint8_t;

/// UTF-32 character / code point
using UniCodePoint = uint32_t;

/// Pair of code point and its length in bytes in UTF-8
struct SUniCodePointLength
{
    UniCodePoint    _CodePoint; // :24
    uint32_t        _ByteLengthUTF8; // :8
};

struct SUTF8CodePoint
{
    UTF8Char Units[4];
    uint8_t NumUnits; // NumUnits==0 indicates an invalid codepoint.
};


/// Helper class providing useful unicode functionality
class ULANGCORE_API CUnicode
{
public:

    // Tables for doing fast whitespace character lookup [Any non-ASCII character will result in false.]
    static const bool _ASCIITable_Whitespace[BYTE_RANGE];     // space, tab, newline, carriage return
    static const bool _ASCIITable_Identifier[BYTE_RANGE];     // A-Z, a-z, 1-9, _

    ULANG_FORCEINLINE static bool IsWhitespaceASCII(const UTF8Char Ch) { return _ASCIITable_Whitespace[Ch]; }
    ULANG_FORCEINLINE static bool IsDigitASCII(const UTF8Char Ch)      { return (unsigned(Ch) - unsigned('0')) < 10u; }
    ULANG_FORCEINLINE static bool IsUpperASCII(const UTF8Char Ch)      { return (unsigned(Ch) - unsigned('A')) < 26u; }
    ULANG_FORCEINLINE static bool IsLowerASCII(const UTF8Char Ch)      { return (unsigned(Ch) - unsigned('a')) < 26u; }
    ULANG_FORCEINLINE static bool IsAlphaASCII(const UTF8Char Ch)      { return ((unsigned(Ch) - unsigned('a')) < 26u) || ((unsigned(Ch) - unsigned('A')) < 26u); }
    ULANG_FORCEINLINE static UTF8Char ToLower_ASCII(const UTF8Char Ch) { return IsUpperASCII(Ch) ? Ch + ('a' - 'A') : Ch; }
    ULANG_FORCEINLINE static UTF8Char ToUpper_ASCII(const UTF8Char Ch) { return IsLowerASCII(Ch) ? Ch - ('a' - 'A') : Ch; }

    static SUniCodePointLength DecodeUTF8(const UTF8Char * Text, size_t TextByteLength);
    static SUTF8CodePoint EncodeUTF8(UniCodePoint CodePoint);

    /// uLang-specific detection of identifier code points
    static bool IsIdentifierStart(UniCodePoint CodePoint); ///< Identifier start?
    static bool IsIdentifierTail(UniCodePoint CodePoint); ///< Identifier continuation/middle?


private:

    static SUniCodePointLength DecodeUTF8NonASCII(const UTF8Char * Text, size_t TextByteLength);

    static bool IsIdentifierStartNonASCII(UniCodePoint CodePoint);
    static bool IsIdentifierTailNonASCII(UniCodePoint CodePoint);

};

//=======================================================================================
// CUnicode Inline Methods
//=======================================================================================

// This inline function optimizes for the most common case that the code point is ASCII
// Only for non-ASCII code points an actual function call is made
ULANG_FORCEINLINE SUniCodePointLength CUnicode::DecodeUTF8(const UTF8Char * Text, size_t TextByteLength)
{
    ULANG_ASSERTF(TextByteLength > 0, "Can't decode UFT8 from empty string!");

    // If ASCII, deal with it right here
    UniCodePoint FirstByte = *Text;
    if (FirstByte < ASCII_RANGE)
    {
        // Handle all ASCII characters inline
        return { FirstByte, 1 };
    }

    // Not ASCII - call the professionals
    return DecodeUTF8NonASCII(Text, TextByteLength);
}

// This inline function optimizes for the most common case that the code point is ASCII
// Only for non-ASCII code points an actual function call is made
ULANG_FORCEINLINE bool CUnicode::IsIdentifierStart(UniCodePoint CodePoint)
{
    // If ASCII, deal with it right here
    if (CodePoint < ASCII_RANGE)
    {
        // Handle all ASCII characters inline
        return ((CodePoint >= 'A' && CodePoint <= 'z' && (CodePoint <= 'Z' || CodePoint >= 'a')) || CodePoint == '_');
    }

    // Not ASCII - call the professionals
    return IsIdentifierStartNonASCII(CodePoint);
}

// This inline function optimizes for the most common case that the code point is ASCII
// Only for non-ASCII code points an actual function call is made
ULANG_FORCEINLINE bool CUnicode::IsIdentifierTail(UniCodePoint CodePoint)
{
    // If ASCII, deal with it right here
    if (CodePoint < ASCII_RANGE)
    {
        // Handle all ASCII characters inline
        return _ASCIITable_Identifier[CodePoint];
    }

    // Not ASCII - call the professionals
    return IsIdentifierTailNonASCII(CodePoint);
}

} // uLang namespace