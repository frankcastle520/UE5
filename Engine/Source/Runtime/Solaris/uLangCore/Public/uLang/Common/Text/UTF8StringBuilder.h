// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Memory/Allocator.h"
#include "uLang/Common/Text/UTF8String.h"
#include <stdio.h> // snprintf
#include <cstring> // strlen

namespace uLang
{

// Indent defaults
static const char *          DefaultIndentCString             = "    ";
static const uint32_t        DefaultIndentCStringByteCount    = 4u;  // Number of bytes for DefaultIndentCString
static const uint32_t        DefaultIndentEffectiveSpaceCount = 4u;
static const CUTF8StringView DefaultIndentString              = CUTF8StringView(DefaultIndentCString);


/// String builder class
/// Used to modify and manipulate strings
template<class AllocatorType, typename... AllocatorArgsType>
class TUTF8StringBuilder
{
public:

    using StringType = TUTF8String<AllocatorType, AllocatorArgsType...>;

    // Construction

    TUTF8StringBuilder(uint32_t ReserveBytes = 0) : _AllocatedBytes(0) { if (ReserveBytes) { EnsureAllocated(ReserveBytes + 1); } }
    TUTF8StringBuilder(const char* NullTerminatedString, AllocatorArgsType&&... AllocatorArgs);
    TUTF8StringBuilder(const CUTF8StringView& StringView, AllocatorArgsType&&... AllocatorArgs);
    template<typename... FormatterArgsType>
    TUTF8StringBuilder(AllocatorArgsType&&... AllocatorArgs, const char* NullTerminatedFormat, FormatterArgsType&&... FormatterArgs);
    TUTF8StringBuilder(const TUTF8StringBuilder& Other);
    TUTF8StringBuilder(TUTF8StringBuilder&& Other);

    ~TUTF8StringBuilder()    {} // Memory will be released via the embedded string object

    void Reset()                                              { _String._String._End = _String._String._Begin; } // Just reset length but hold on to memory
    void EnsureAllocatedExtra(size_t ExtraBytes)              { EnsureAllocated(_String.ByteLen() + ExtraBytes); }


    // Accessors

    ULANG_FORCEINLINE int32_t ByteLen() const                 { return _String.ByteLen(); }
    ULANG_FORCEINLINE bool IsEmpty() const                    { return _String.IsEmpty(); }
    ULANG_FORCEINLINE bool IsFilled() const                   { return _String.IsFilled(); }

    /// @return specific byte from this string
    ULANG_FORCEINLINE const UTF8Char& operator[](int32_t ByteIndex) const
    {
        return _String[ByteIndex];
    }

    /// @return the last byte in this string (UTF-8 agnostic) or null character if empty
    ULANG_FORCEINLINE UTF8Char LastByte() const               { return _String.ToStringView().LastByte(); }

    // Comparison operators

    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator==(const TUTF8StringBuilder<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String == Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator!=(const TUTF8StringBuilder<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String != Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator==(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String == Other; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator!=(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String != Other; }
    ULANG_FORCEINLINE bool operator==(const CUTF8StringView& StringView) const { return _String == StringView; }
    ULANG_FORCEINLINE bool operator!=(const CUTF8StringView& StringView) const { return _String != StringView; }

    // Assignment

    TUTF8StringBuilder& operator=(const TUTF8StringBuilder& Other);
    TUTF8StringBuilder& operator=(TUTF8StringBuilder&& Other);
    TUTF8StringBuilder& operator=(const CUTF8StringView& StringView);

    // Conversions

    ULANG_FORCEINLINE operator const CUTF8StringView&() const     { return _String; }
    ULANG_FORCEINLINE const CUTF8StringView& ToStringView() const { return _String; }
    ULANG_FORCEINLINE const char* AsCString() const               { return _String.AsCString(); }
    ULANG_FORCEINLINE const char* operator*() const               { return _String.AsCString(); }

    StringType  MoveToString(); // Move string and reset the builder (most efficient)
    StringType  CopyToString() const { return _String; } // Make copy of string and keep builder as-is (less efficient)

    // Modifications

    TUTF8StringBuilder& Append(UTF8Char Char);
    TUTF8StringBuilder& Append(const CUTF8StringView& String);
    template<typename... FormatterArgsType>
    TUTF8StringBuilder& AppendFormat(const char* NullTerminatedFormat, FormatterArgsType&&... FormatterArgs);
    /**
     * Grows the String for serialization purposes -- the _End pointer is moved
     * to the end of the buffer, expecting serialization to occur immediately following this call.
     *
     * NOTE: This inserts a terminating '\0' character preliminary into the
     *       buffer, but expects serialization to overwrite it with a '\0' of its own.
     *
     * @return A pointer to the appended buffer's start -- useful for subsequent serialization.
     **/
    UTF8Char* AppendBuffer(size_t ByteSize);

    /** Trim character from end if exists */
    TUTF8StringBuilder& TrimEnd(UTF8Char Ch);

    /** Replace a range of bytes with the provided replacement string */
    TUTF8StringBuilder& ReplaceRange(SIdxRange ToBeReplaced, const CUTF8StringView& Replacement);

    /** Replace a single character with the provided character */
    TUTF8StringBuilder& ReplaceAt(int32_t Index, const UTF8Char Replacement);

    /** Replace all instances of a single character with the provided character */
    TUTF8StringBuilder& ReplaceAll(const UTF8Char Search, const UTF8Char Replacement);

    /** Inserts a string before the given index. */
    TUTF8StringBuilder& InsertAt(int32_t Index, const CUTF8StringView& StringToInsert);

    /**
     * Indents rows/lines by specified `SpaceCount` spaces from `Idx` over the span of
     * `Count` characters using behavior similar to the MS Visual Studio editor.
     * Works on line breaks that are in Unix style `\n` or DOS style `\r\n`.
     *
     * @returns: number of line breaks over the specified range
     * @param: StartIdx - starting index to begin indentation. If negative it indicates index position from end of string - so -1 = last char, -2 = char before last, etc.
     * @param: SpanCount - Span of characters to indent. If negative it indicates remainder of string after `Idx` - so -1 = include last, -2 = include char before last, etc.
     * @param: SpaceCount - number of space characters to indent
     * @notes:
     *     - Spaces inserted at the beginning of each row.
     *     - Rows with no non-space characters are not indented.
     *     - If the range ends just after a line break, the following row is not indented - at
     *       least one character on a row must be included for it to be indented
     *     - Similar to Visual Studio editor behavior.
     **/
    int32_t LineIndentEditor(int32_t Idx = 0, int32_t SpanCount = -1, int32_t SpaceCount = DefaultIndentEffectiveSpaceCount);

protected:

    ULANG_FORCEINLINE int32_t InputByteIdxToDirectIdx(int32_t InIdx) const                   { return _String._String.InputByteIdxToDirectIdx(InIdx); }
    ULANG_FORCEINLINE bool    InputByteIdxSpan(int32_t& InOutIdx, int32_t& InOutSpan) const  { return _String._String.InputByteIdxSpan(InOutIdx, InOutSpan); }

private:

    /// Compute allocation size from requested size
    ULANG_FORCEINLINE static size_t CalculateBytesToAllocate(size_t RequestedBytes)
    {
        // For now, we just grow in fixed increments of big chunks
        // Exponential growth of small chunks does not seem like a good idea
        constexpr size_t Alignment = 1 << 11; // 2K
        return (RequestedBytes + (Alignment - 1)) & ~(Alignment - 1);
    }

    /// Initialize from a given char array
    ULANG_FORCEINLINE void Construct(const UTF8Char* String, size_t ByteLength, const AllocatorType& Allocator)
    {
        size_t BytesToAllocate = CalculateBytesToAllocate(ByteLength + 1);
        UTF8Char* Memory = (UTF8Char*)Allocator.Allocate(BytesToAllocate);
        _String._String._Begin = Memory;
        _String._String._End = Memory + ByteLength;
        if (ByteLength)
        {
            memcpy(Memory, String, ByteLength);
        }
        Memory[ByteLength] = 0; // Add null termination
        _String.GetAllocator() = Allocator; // Remember the allocator
        _AllocatedBytes = uint32_t(BytesToAllocate);
    }

    /// Ensure string builder has a given size
    ULANG_FORCEINLINE void EnsureAllocated(size_t BytesNeeded)
    {
        // Do we need to grow?
        BytesNeeded = CalculateBytesToAllocate(BytesNeeded);
        if (BytesNeeded > _AllocatedBytes)
        {
            // Yes, grow our allocation
            UTF8Char* Memory = (UTF8Char*)_String.GetAllocator().Reallocate((void*)_String._String._Begin, BytesNeeded);
            _String._String._End = Memory + _String.ByteLen();
            _String._String._Begin = Memory;
            _AllocatedBytes = uint32_t(BytesNeeded);
        }
    }

    // We repurpose a TUTF8String to store the string
    StringType _String;

    // How many bytes we have actually allocated
    uint32_t _AllocatedBytes;
};

/// A string allocated on the heap
using CUTF8StringBuilder = TUTF8StringBuilder<CHeapRawAllocator>;

/// A string allocated using a given allocator instance
using CUTF8StringBuilderA = TUTF8StringBuilder<CInstancedRawAllocator, CAllocatorInstance*>;

//=======================================================================================
// TUTF8StringBuilder Inline Methods
//=======================================================================================

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::TUTF8StringBuilder(const char* NullTerminatedString, AllocatorArgsType&&... AllocatorArgs)
    : _String(NoInit)
{
    Construct((const UTF8Char*)NullTerminatedString, ::strlen(NullTerminatedString), AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...));
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::TUTF8StringBuilder(const CUTF8StringView& StringView, AllocatorArgsType&&... AllocatorArgs)
    : _String(NoInit)
{
    Construct(StringView._Begin, StringView.ByteLen(), AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...));
}

ULANG_SILENCE_SECURITY_WARNING_START
template<class AllocatorType, typename... AllocatorArgsType>
template<typename... FormatterArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::TUTF8StringBuilder(AllocatorArgsType&&... AllocatorArgs, const char* NullTerminatedFormat, FormatterArgsType&&... FormatterArgs)
    : _String(NoInit)
{
    // Compute length of string
    size_t ByteLength = ::snprintf(nullptr, 0, NullTerminatedFormat, FormatterArgs...);

    // Allocate memory
    AllocatorType Allocator(AllocatorArgs...);
    size_t BytesToAllocate = CalculateBytesToAllocate(ByteLength + 1);
    UTF8Char* Text = (UTF8Char*)Allocator.Allocate(BytesToAllocate);

    // Create string
    ::snprintf((char*)Text, BytesToAllocate, NullTerminatedFormat, FormatterArgs...);

    // Store string and allocator
    _String._String = CUTF8StringView(Text, Text + ByteLength);
    _String.GetAllocator() = Allocator;
    _AllocatedBytes = uint32_t(BytesToAllocate);
}
ULANG_SILENCE_SECURITY_WARNING_END

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::TUTF8StringBuilder(const TUTF8StringBuilder& Other)
    : _String(NoInit)
{
    Construct(Other._String._String._Begin, Other.ByteLen(), Other._String.GetAllocator());
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::TUTF8StringBuilder(TUTF8StringBuilder&& Other)
    : _String(ForwardArg<StringType>(Other._String))
    , _AllocatedBytes(Other._AllocatedBytes)
{
    Other._AllocatedBytes = 0;
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...> TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::MoveToString()
{
    StringType String(NoInit);
    if (_String.IsFilled())
    {
        size_t ByteLength = ByteLen();
        UTF8Char* Memory = (UTF8Char*)_String.GetAllocator().Reallocate((void*)_String._String._Begin, ByteLength + 1);
        String._String._Begin = Memory;
        String._String._End = Memory + ByteLength;
        _String._String.Reset();
    }
    else
    {
        _String.Reset(); // Free potential slack memory
        String._String.Reset();
    }

    String.GetAllocator() = _String.GetAllocator();
    _AllocatedBytes = 0;

    return String;
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::operator=(const TUTF8StringBuilder& Other)
{
    int32_t ByteLen = Other.ByteLen();
    if (ByteLen)
    {
        EnsureAllocated(ByteLen + 1u);
        memcpy(const_cast<UTF8Char*>(_String._String._Begin), Other._String._String._Begin, ByteLen + 1u);
    }
    // Note, no need to write null terminator if empty if _Begin == _End then "" returned by AsCString()
    _String._String._End = (UTF8Char*)_String._String._Begin + ByteLen;
    _String.GetAllocator() = Other._String.GetAllocator();
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::operator=(TUTF8StringBuilder&& Other)
{
    _String = Move(Other._String);
    _AllocatedBytes = Other._AllocatedBytes;
    Other._AllocatedBytes = 0;
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::operator=(const CUTF8StringView& StringView)
{
    int32_t ByteLen = StringView.ByteLen();
    if (ByteLen)
    {
        EnsureAllocated(ByteLen + 1u);
        memcpy((UTF8Char*)_String._String._Begin, StringView._Begin, ByteLen);
        UTF8Char* WritableEnd = (UTF8Char*)_String._String._Begin + ByteLen;
        *WritableEnd = 0;
        _String._String._End = WritableEnd;
        return *this;
    }
    // No need to write null terminator if empty if _Begin == _End then "" returned by AsCString()
    _String._String._End = (UTF8Char*)_String._String._Begin;
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
inline TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::Append(UTF8Char Char)
{
    EnsureAllocated(ByteLen() + 2);
    UTF8Char* WritableEnd = (UTF8Char*)_String._String._End; // Local variable to avoid LHS
    *WritableEnd++ = Char;
    *WritableEnd = 0;
    _String._String._End = WritableEnd;
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
inline TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::Append(const CUTF8StringView& String)
{
    size_t OtherLength = String.ByteLen();
    if (OtherLength)
    {
        EnsureAllocated(ByteLen() + OtherLength + 1);
        UTF8Char* WritableEnd = (UTF8Char*)_String._String._End; // Local variable to avoid LHS
        memcpy(WritableEnd, String._Begin, OtherLength);
        WritableEnd += OtherLength;
        *WritableEnd = 0;
        _String._String._End = WritableEnd;
    }
    return *this;
}

ULANG_SILENCE_SECURITY_WARNING_START
template<class AllocatorType, typename... AllocatorArgsType>
template<typename... FormatterArgsType>
inline TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::AppendFormat(const char* NullTerminatedFormat, FormatterArgsType&&... FormatterArgs)
{
    // Compute length of string
    size_t ByteLength = ::snprintf(nullptr, 0, NullTerminatedFormat, FormatterArgs...);

    // Allocate memory
    EnsureAllocated(ByteLen() + ByteLength + 1);

    // Create string
    ::snprintf((char*)_String._String._End, _AllocatedBytes, NullTerminatedFormat, FormatterArgs...);

    // Increase length
    _String._String._End += ByteLength;

    return *this;
}
ULANG_SILENCE_SECURITY_WARNING_END

template<class AllocatorType, typename... AllocatorArgsType>
UTF8Char* TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::AppendBuffer(size_t ByteSize)
{
    EnsureAllocated(ByteLen() + ByteSize + 1);
    UTF8Char* BufferStart = (UTF8Char*)_String._String._End;

    // We're growing the string, such that it can be serialized into -- in that regard,
    // we want the _End to point to the end of the buffer
    UTF8Char* NewEnd = (UTF8Char*)( ((uint8_t*)BufferStart) + ByteSize );
    _String._String._End = NewEnd;

    // Zero, just to guard against users failing to serialize into this buffer immediately
    *BufferStart = 0;
    *NewEnd = 0;

    return BufferStart;
}

template<class AllocatorType, typename... AllocatorArgsType>
TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::ReplaceRange(SIdxRange ToBeReplaced, const CUTF8StringView& Replacement)
{
    ULANG_ASSERTF(ToBeReplaced._Begin <= uint32_t(ByteLen()) && ToBeReplaced._End <= uint32_t(ByteLen()) && ToBeReplaced._Begin <= ToBeReplaced._End, "Malformed index range.");

    size_t NewLength = ByteLen() - ToBeReplaced.GetLength() + Replacement.ByteLen();
    EnsureAllocated(NewLength + 1);
    memmove(const_cast<UTF8Char*>(_String._String._Begin) + ToBeReplaced._Begin + Replacement.ByteLen(), _String._String._Begin + ToBeReplaced._End, ByteLen() + 1 - ToBeReplaced._End);
    if (Replacement.ByteLen())
    {
        memcpy(const_cast<UTF8Char*>(_String._String._Begin) + ToBeReplaced._Begin, Replacement._Begin, Replacement.ByteLen());
    }
    _String._String._End = _String._String._Begin + NewLength;
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::ReplaceAt(int32_t Index, const UTF8Char Replacement)
{
    ULANG_ASSERTF(Index >= 0 && Index <= ByteLen(), "Out-of-bounds index.");
    const_cast<UTF8Char*>(_String._String._Begin)[Index] = Replacement;
    return *this;
}

template <class AllocatorType, typename... AllocatorArgsType>
TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::ReplaceAll(const UTF8Char Search, const UTF8Char Replacement)
{
    for (UTF8Char* Ch = const_cast<UTF8Char*>(_String._String._Begin); Ch < _String._String._End; ++Ch)
    {
        if (*Ch == Search)
        {
            *Ch = Replacement;
        }
    }
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::InsertAt(int32_t Index, const CUTF8StringView& StringToInsert)
{
    ULANG_ASSERTF(Index >= 0 && Index <= ByteLen(), "Out-of-bounds index.");

    size_t NewLength = ByteLen() + StringToInsert.ByteLen();
    EnsureAllocated(NewLength + 1);
    memmove(const_cast<UTF8Char*>(_String._String._Begin) + Index + StringToInsert.ByteLen(), _String._String._Begin + Index, ByteLen() - Index);
    if (StringToInsert.ByteLen())
    {
        memcpy(const_cast<UTF8Char*>(_String._String._Begin) + Index, StringToInsert._Begin, StringToInsert.ByteLen());
    }
    _String._String._End = _String._String._Begin + NewLength;
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>& TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::TrimEnd(UTF8Char Ch)
{
    if (_String._String.IsFilled() && _String._String._End[-1] == Ch)
    {
        ((UTF8Char*)(--_String._String._End))[0] = 0;
    }
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
inline int32_t TUTF8StringBuilder<AllocatorType, AllocatorArgsType...>::LineIndentEditor(int32_t Idx, int32_t SpanCount, int32_t SpaceCount)
{
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Resolve span and determine if there is anything to do
    if (!InputByteIdxSpan(Idx, SpanCount))
    {
        return 0;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Determine number of indented lines to determine how much more space needed
    UTF8Char* CStr        = const_cast<UTF8Char*>(_String._String._Begin) + Idx;
    UTF8Char* CStrEnd     = CStr + SpanCount;
    int32_t   LineCount   = 1; // First line counts as #1
    int32_t   IndentCount = 0; // Not every line may be indented
    bool      bNonWS      = false; // Found preceding non-whitespace before newline

    while (CStr < CStrEnd)
    {
        switch (*CStr)
        {
            case '\n':
                if (bNonWS)
                {
                    IndentCount++;
                    bNonWS = false;
                }

                LineCount++;
                break;

            case ' ':
            case '\t':
            case '\r':
                // Ignored whitespace other than newline
                break;

            default:
                // Only indent lines that have non-whitespace in them
                bNonWS = true;
        }

        CStr++;
    }

    if (bNonWS)
    {
        IndentCount++;
    }

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Ensure enough memory for indenting
    int32_t ExtraBytes = IndentCount * SpaceCount;

    EnsureAllocatedExtra(ExtraBytes);

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Move mem from: range end - old end to: range end new - end new
    size_t Bytes = ByteLen() - (Idx + SpanCount) + 1u;  // Extra char for null terminator
    UTF8Char* CStrStart = const_cast<UTF8Char*>(_String._String._Begin);
    CStrEnd = CStrStart + Idx + SpanCount;
    UTF8Char* CStrDest  = CStrEnd + ExtraBytes;
    memmove(CStrDest, CStrEnd, Bytes);

    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // Work in reverse over selected section (so bytes only copied once)
    UTF8Char* CStrResume;
    UTF8Char* CStrBegin = CStrStart + Idx;  // Beginning of selection
    CStr = CStrEnd - 1;
    do
    {
        // Read reverse until \n or start of selection found
        bNonWS = false;
        while ((CStr > CStrBegin) && (*CStr != '\n'))
        {
            if ((*CStr != ' ') && (*CStr != '\t') && (*CStr != '\r'))
            {
                bNonWS = true;
            }

            CStr--;
        }

        CStrResume = CStr;

        if (*CStr == '\n')
        {
            // Skip over newline
            CStr++;
        }

        Bytes = size_t(CStrEnd - CStr);

        // Move section to end
        CStrDest -= Bytes;
        CStrEnd -= Bytes;
        memmove(CStrDest, CStr, Bytes);

        // Don't indent rows with no content
        if (bNonWS)
        {
            // Write indent chars
            CStrDest -= SpaceCount;
            if (SpaceCount)
            {
                memset(CStrDest, ' ', SpaceCount);
            }
        }

        // Keep scanning backward
        CStr = CStrResume - 1;
    } while (CStr >= CStrBegin);

    // Fix up the internals
    _String._String._End += ExtraBytes;

    return LineCount;
}


}  // namespace uLang
