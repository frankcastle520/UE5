// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Memory/Allocator.h"
#include "uLang/Common/Text/UTF8StringView.h"
#include <stdarg.h> // va_start, va_copy, va_end
#include <stdio.h> // snprintf
#include <cstring> // strlen

namespace uLang
{

template<class AllocatorType, typename... AllocatorArgsType> class TUTF8StringBuilder;

/// Simple string class, used mostly for string storage
/// Implements null termination, so can be used as a C-style string (via operator *)
/// The AllocatorType must provide the methods void * Allocate(size_t) and void Deallocate(void *)
template<class AllocatorType, typename... AllocatorArgsType>
class TUTF8String : AllocatorType
{
public:

    // Construction

    TUTF8String() : AllocatorType(DefaultInit) {}
    TUTF8String(const char* NullTerminatedString, AllocatorArgsType&&... AllocatorArgs);
    TUTF8String(const CUTF8StringView& StringView, AllocatorArgsType&&... AllocatorArgs);
    template<typename... FormatterArgsType>
    TUTF8String(AllocatorArgsType&&... AllocatorArgs, const char* NullTerminatedFormat, FormatterArgsType&&... FormatterArgs);
    TUTF8String(AllocatorArgsType&&... AllocatorArgs, const char* NullTerminatedFormat, va_list FormatterArgs);
    template<typename InitializerFunctorType> // Functor takes UTF8Char* pointer to uninitialized memory
    TUTF8String(size_t ByteLength, InitializerFunctorType&& InitializerFunctor, AllocatorArgsType&&... AllocatorArgs);
    TUTF8String(const TUTF8String& Other);
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    TUTF8String(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other, AllocatorArgsType&&... AllocatorArgs);
    TUTF8String(TUTF8String&& Other);

    ~TUTF8String()  { if (_String._Begin) { Release(); } }
    ULANG_FORCEINLINE void Reset()    { if (_String._Begin) { Release(); _String.Reset(); } }
    ULANG_FORCEINLINE void Empty()    { Reset(); }
    ULANG_FORCEINLINE UTF8Char* Resize(int32_t NewByteLen) { return Reallocate(NewByteLen); }

    // Accessors

    ULANG_FORCEINLINE int32_t ByteLen() const                 { return _String.ByteLen(); }
    ULANG_FORCEINLINE bool IsEmpty() const                    { return _String.IsEmpty(); }
    ULANG_FORCEINLINE bool IsFilled() const                   { return _String.IsFilled(); }
    ULANG_FORCEINLINE const UTF8Char* AsUTF8() const          { return _String._Begin ? _String._Begin : (const UTF8Char*)""; }
    ULANG_FORCEINLINE const char* AsCString() const           { return _String._Begin ? (char*)_String._Begin : ""; }
    ULANG_FORCEINLINE const char* operator*() const           { return _String._Begin ? (char*)_String._Begin : ""; }

    /// @return specific byte from this string
    ULANG_FORCEINLINE const UTF8Char& operator[](int32_t ByteIndex) const
    {
        ULANG_ASSERTF(ByteIndex >= 0 && _String._Begin + ByteIndex < _String._End, "Invalid index: Index=%i, ByteLen()=%i", ByteIndex, ByteLen());
        return _String._Begin[ByteIndex];
    }

    // Comparison operators

    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator==(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String == Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator!=(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String != Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator<(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String < Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator<=(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String <= Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator>(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String > Other._String; }
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    ULANG_FORCEINLINE bool operator>=(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const { return _String >= Other._String; }
    ULANG_FORCEINLINE bool operator==(const CUTF8StringView& StringView) const { return _String == StringView; }
    ULANG_FORCEINLINE bool operator!=(const CUTF8StringView& StringView) const { return _String != StringView; }

    // Assignment

    TUTF8String& operator=(const TUTF8String& Other);
    TUTF8String& operator=(TUTF8String&& Other);

    // Append
    TUTF8String& operator+=(const CUTF8StringView& OtherStringView);
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    TUTF8String& operator+=(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other);
    TUTF8String& operator+=(const char* OtherNullTerminatedString);

    TUTF8String operator+(const CUTF8StringView& OtherStringView) const;
    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    TUTF8String operator+(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const;
    TUTF8String operator+(const char* OtherNullTerminatedString) const;

    // Conversions

    ULANG_FORCEINLINE operator const CUTF8StringView&() const     { return _String; }
    ULANG_FORCEINLINE const CUTF8StringView& ToStringView() const { return _String; }

    // Unicode iteration

    ULANG_FORCEINLINE CUTF8StringView::UnicodeConstIterator begin() const { return _String.begin(); }
    ULANG_FORCEINLINE CUTF8StringView::UnicodeConstIterator end() const { return _String.end(); }

    // Misc

    ULANG_FORCEINLINE TUTF8String Replace(UTF8Char Old, UTF8Char New) const
    {
        return TUTF8String(ByteLen(), [this, Old, New](UTF8Char* Memory)
            {
                for (const UTF8Char* OldCh = _String._Begin; OldCh < _String._End; ++OldCh)
                {
                    *Memory++ = (*OldCh == Old) ? New : *OldCh;
                }
            });
    }

    TUTF8String Replace(const CUTF8StringView& Old, const CUTF8StringView& New) const
    {
        // Anything to do?
        if (IsEmpty() || Old.IsEmpty())
        {
            return *this;
        }

        TUTF8String Result(NoInit, GetAllocator());
        Result.AllocateUninitialized(ByteLen() * 2);
        UTF8Char* DstChar = const_cast<UTF8Char*>(Result._String._Begin);

        // Find matches
        const UTF8Char* BeginChar = _String._Begin;
        const UTF8Char* EndChar = _String._End - Old.ByteLen(); // No need to check further as Text wouldn't fit
        for (const UTF8Char* ThisChar = BeginChar; ThisChar <= EndChar; ++ThisChar)
        {
            const UTF8Char* SubChar = ThisChar;
            for (const UTF8Char* OldChar = Old._Begin; OldChar < Old._End; ++OldChar, ++SubChar)
            {
                if (*SubChar != *OldChar)
                {
                    goto Continue;
                }
            }

            // We found an occurrence! Copy partial result and continue...

            // Will it fit?
            {
                // BytesNeeded includes the bytes for this partial result plus the remainder part
                intptr_t BytesNeeded = (ThisChar - BeginChar + New.ByteLen()) + (_String._End - SubChar);
                if (DstChar + BytesNeeded > Result._String._End)
                {
                    // No, double the size of the result
                    intptr_t DstOffset = DstChar - Result._String._Begin;
                    Result._String._End = DstChar; // So we won't copy unnecessary bytes to the new memory
                    Result.Reallocate(Result.ByteLen() * 2 + BytesNeeded);
                    DstChar = const_cast<UTF8Char*>(Result._String._Begin + DstOffset);
                }
            }

            // Copy partial result
            ::memcpy(DstChar, BeginChar, ThisChar - BeginChar);
            DstChar += (ThisChar - BeginChar);
            if (New.ByteLen())
            {
                ::memcpy(DstChar, New._Begin, New.ByteLen());
            }
            DstChar += New.ByteLen();

            // And continue
            BeginChar = ThisChar = SubChar;
            --ThisChar; // It's going to be incremented by the loop

        Continue:;
        }

        // Copy remainder part after last replacement
        // We have made sure in the code above that there is enough space for it
        ::memcpy(DstChar, BeginChar, _String._End - BeginChar);
        DstChar += (_String._End - BeginChar);

        // Shrink result to perfect fit
        Result.Reallocate(DstChar - Result._String._Begin);
        return Result;
    }

    static const TUTF8String& GetEmpty();

protected:

    ULANG_FORCEINLINE int32_t InputByteIdxToDirectIdx(int32_t InIdx) const                   { return _String.InputByteIdxToDirectIdx(InIdx); }
    ULANG_FORCEINLINE bool    InputByteIdxSpan(int32_t& InOutIdx, int32_t& InOutSpan) const  { return _String.InputByteIdxSpan(InOutIdx, InOutSpan); }

private:

    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    friend class TUTF8StringBuilder;

    TUTF8String(ENoInit) {} // Do nothing - friends only!
    TUTF8String(ENoInit, const AllocatorType& Allocator) : AllocatorType(Allocator), _String(NoInit) {}

    ULANG_FORCEINLINE UTF8Char* AllocateUninitialized(size_t ByteLength)
    {
        UTF8Char* Memory = (UTF8Char*)GetAllocator().Allocate(ByteLength + 1);
        _String._Begin = Memory;
        _String._End = Memory + ByteLength;

        return Memory;
    }

    ULANG_FORCEINLINE UTF8Char* Reallocate(size_t ByteLength)
    {
        size_t OldLength = ByteLen();

        UTF8Char* NewMemory = (UTF8Char*)GetAllocator().Allocate(ByteLength + 1);
        
        if (_String._Begin)
        {
            memcpy(NewMemory, _String._Begin, (ByteLength < OldLength ? ByteLength : OldLength));
            Release();
        }

        _String._Begin = NewMemory;
        _String._End = NewMemory + ByteLength;

        // re-null-terminate
        NewMemory[ByteLength] = 0;

        return NewMemory;
    }

    /// Initialize from a given char array
    ULANG_FORCEINLINE void AllocateInitialized(const UTF8Char* String, size_t ByteLength)
    {
        UTF8Char* Memory = AllocateUninitialized(ByteLength);

        if (ByteLength)
        {
            memcpy(Memory, String, ByteLength);
        }
        Memory[ByteLength] = 0; // Add null termination
    }

    /// Let go of our data
    ULANG_FORCEINLINE void Release()
    {
        GetAllocator().Deallocate((void*)_String._Begin);
    }

    AllocatorType& GetAllocator()
    {
        return *this;
    }

    const AllocatorType& GetAllocator() const
    {
        return *this;
    }

    /// The text storage
    CUTF8StringView _String;
};

template<class AllocatorType, typename... AllocatorArgsType>
const TUTF8String<AllocatorType, AllocatorArgsType...>& TUTF8String<AllocatorType, AllocatorArgsType...>::GetEmpty()
{
    static TUTF8String Empty;
    return Empty;
}

/// A string allocated on the heap
using CUTF8String = uLang::TUTF8String<CHeapRawAllocator>;

/// A string allocated using a given allocator instance
using CUTF8StringA = uLang::TUTF8String<CInstancedRawAllocator, CAllocatorInstance*>;

/// Hash function for maps, sets
template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE uint32_t GetTypeHash(const TUTF8String<AllocatorType, AllocatorArgsType...>& String) { return GetTypeHash(String.ToStringView()); }

//=======================================================================================
// TUTF8String Inline Methods
//=======================================================================================

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(const char* NullTerminatedString, AllocatorArgsType&&... AllocatorArgs)
    : AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    , _String(NoInit)
{
    AllocateInitialized((const UTF8Char*)NullTerminatedString, ::strlen(NullTerminatedString));
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(const CUTF8StringView& StringView, AllocatorArgsType&&... AllocatorArgs)
    : AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    , _String(NoInit)
{
    AllocateInitialized(StringView._Begin, StringView.ByteLen());
}

template<class AllocatorType, typename... AllocatorArgsType>
template<typename... FormatterArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(AllocatorArgsType&&... AllocatorArgs, const char* NullTerminatedFormat, FormatterArgsType&&... FormatterArgs)
    : AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    , _String(NoInit)
{
    // Compute length of string
    size_t ByteLength = ::snprintf(nullptr, 0, NullTerminatedFormat, FormatterArgs...);

    // Sanity check that everything went well
    ULANG_ASSERTF(ByteLength != size_t(-1), "Invalid format string: %s", NullTerminatedFormat);

    // Allocate memory
    size_t AllocBytes = ByteLength + 1;
    UTF8Char* Text = (UTF8Char*)GetAllocator().Allocate(AllocBytes);

    // Create string
    ::snprintf((char*)Text, AllocBytes, NullTerminatedFormat, FormatterArgs...);

    // Store string and allocator
    _String = CUTF8StringView(Text, Text + ByteLength);
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(AllocatorArgsType&&... AllocatorArgs, const char* NullTerminatedFormat, va_list FormatterArgs)
    : AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    , _String(NoInit)
{
    va_list FormatterArgsLocal;

    // Compute length of string
    va_copy(FormatterArgsLocal, FormatterArgs);
    size_t ByteLength = ::vsnprintf(nullptr, 0, NullTerminatedFormat, FormatterArgsLocal);
    va_end(FormatterArgsLocal);

    // Sanity check that everything went well
    ULANG_ASSERTF(ByteLength != size_t(-1), "Invalid format string: %s", NullTerminatedFormat);

    // Allocate memory
    size_t AllocBytes = ByteLength + 1;
    UTF8Char* Text = (UTF8Char*)GetAllocator().Allocate(AllocBytes);

    // Create string
    va_copy(FormatterArgsLocal, FormatterArgs);
    ::vsnprintf((char*)Text, AllocBytes, NullTerminatedFormat, FormatterArgsLocal);
    va_end(FormatterArgsLocal);

    // Store string and allocator
    _String = CUTF8StringView(Text, Text + ByteLength);
}

template<class AllocatorType, typename... AllocatorArgsType>
template<typename InitializerFunctorType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(size_t ByteLength, InitializerFunctorType&& InitializerFunctor, AllocatorArgsType&&... AllocatorArgs)
    : AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    , _String(NoInit)
{
    ULANG_ASSERTF(ByteLength <= INT32_MAX, "TUTF8String doesn't support ByteLength > INT32_MAX. (ByteLength=%zu)", ByteLength);

    UTF8Char* Memory = (UTF8Char*)GetAllocator().Allocate(ByteLength + 1);
    _String._Begin = Memory;
    _String._End = Memory + ByteLength;
    InitializerFunctor(Memory);
    Memory[ByteLength] = 0; // Add null termination
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(const TUTF8String& Other)
    : AllocatorType(Other.GetAllocator())
    , _String(NoInit)
{
    AllocateInitialized(Other._String._Begin, Other.ByteLen());
}

template<class AllocatorType, typename... AllocatorArgsType>
template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other, AllocatorArgsType&&... AllocatorArgs)
    : AllocatorType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    , _String(NoInit)
{
    AllocateInitialized(Other._String._Begin, Other.ByteLen());
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>::TUTF8String(TUTF8String&& Other)
    : AllocatorType(Other.GetAllocator())
    , _String(Other._String)
{
    Other._String.Reset();
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>& TUTF8String<AllocatorType, AllocatorArgsType...>::operator=(const TUTF8String& Other)
{
    if (_String._Begin) { Release(); }
    GetAllocator() = Other.GetAllocator();
    AllocateInitialized(Other._String._Begin, Other.ByteLen());
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>& TUTF8String<AllocatorType, AllocatorArgsType...>::operator=(TUTF8String&& Other)
{
    if (_String._Begin) { Release(); }
    GetAllocator() = Other.GetAllocator();
    _String = Other._String;
    Other._String.Reset();
    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>& TUTF8String<AllocatorType, AllocatorArgsType...>::operator+=(const CUTF8StringView& OtherStringView)
{
    size_t OldStringLength = ByteLen();
    size_t NewStringLength = OldStringLength + OtherStringView.ByteLen();

    UTF8Char* NewMemory = Reallocate(NewStringLength);
    if (OtherStringView.ByteLen())
    {
        memcpy(NewMemory + OldStringLength, OtherStringView.Data(), OtherStringView.ByteLen());
    }

    return *this;
}

template<class AllocatorType, typename... AllocatorArgsType>
template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>& TUTF8String<AllocatorType, AllocatorArgsType...>::operator+=(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other)
{
    return (*this) += Other.ToStringView();
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...>& TUTF8String<AllocatorType, AllocatorArgsType...>::operator+=(const char* OtherNullTerminatedString)
{
    size_t OtherStringLength = ::strlen(OtherNullTerminatedString);
    return (*this) += CUTF8StringView(OtherNullTerminatedString, OtherStringLength);
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...> TUTF8String<AllocatorType, AllocatorArgsType...>::operator+(const CUTF8StringView& OtherStringView) const
{
    TUTF8String<AllocatorType, AllocatorArgsType...> Result = *this;
    return Result += OtherStringView;
}

template<class AllocatorType, typename... AllocatorArgsType>
template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...> TUTF8String<AllocatorType, AllocatorArgsType...>::operator+(const TUTF8String<OtherAllocatorType, OtherAllocatorArgsType...>& Other) const
{
    return (*this) + Other.ToStringView();
}

template<class AllocatorType, typename... AllocatorArgsType>
ULANG_FORCEINLINE TUTF8String<AllocatorType, AllocatorArgsType...> TUTF8String<AllocatorType, AllocatorArgsType...>::operator+(const char* OtherNullTerminatedString) const
{
    size_t OtherStringLength = ::strlen(OtherNullTerminatedString);
    return (*this) + CUTF8StringView(OtherNullTerminatedString, OtherStringLength);
}

}
