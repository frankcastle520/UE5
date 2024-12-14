// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Misc/CRC.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Text/IdxRange.h"
#include "uLang/Common/Text/Unicode.h"
#include <cstring>

namespace uLang
{

class CUTF8StringView
{
public:
    // Public Data

    const UTF8Char* _Begin;  ///< Points to first byte
    const UTF8Char* _End;    ///< Points to the byte after the last byte

    // Construction

    CUTF8StringView() : _Begin(nullptr), _End(nullptr) {}
    CUTF8StringView(ENoInit) {} // Do nothing - use with care!
    CUTF8StringView(const UTF8Char* Begin, const UTF8Char* End) : _Begin(Begin), _End(End) {}
    CUTF8StringView(const char * NullterminatedString) : _Begin((UTF8Char *)NullterminatedString), _End((UTF8Char *)NullterminatedString + ::strlen(NullterminatedString)) {}
    CUTF8StringView(const char * String, size_t ByteLen) : _Begin((UTF8Char *)String), _End((UTF8Char *)String + ByteLen) {}

    void Reset()                                          { _Begin = _End = nullptr; }
    void Set(const UTF8Char* Begin, const UTF8Char* End)  { _Begin = Begin; _End = End; }

    // Accessors
    ULANG_FORCEINLINE const UTF8Char* Data() const        { return _Begin; }
    ULANG_FORCEINLINE int32_t ByteLen() const             { return int32_t(_End - _Begin); }
    ULANG_FORCEINLINE bool IsEmpty() const                { return _Begin >= _End; }
    ULANG_FORCEINLINE bool IsFilled() const               { return _Begin < _End; }

    /// @return specific byte from this string
    ULANG_FORCEINLINE const UTF8Char& operator[](int32_t ByteIndex) const
    {
        ULANG_ASSERTF(ByteIndex >= 0 && _Begin + ByteIndex < _End, "Invalid index: ByteIndex=%i ByteLen()=%i", ByteIndex, ByteLen());
        return _Begin[ByteIndex];
    }

    /// @return the first byte in this string (UTF-8 agnostic) or null character if empty
    ULANG_FORCEINLINE UTF8Char FirstByte() const
    {
        return (_End > _Begin) ? *_Begin : 0u;
    }

    /// @return the second byte in this string (UTF-8 agnostic) or null character if no such character
    ULANG_FORCEINLINE UTF8Char SecondByte() const
    {
        return (_End > (_Begin + 1)) ? *(_Begin + 1) : 0u;
    }

    /// @return the last byte in this string (UTF-8 agnostic) or null character if empty
    ULANG_FORCEINLINE UTF8Char LastByte() const
    {
        return (_End > _Begin) ? *(_End - 1) : 0u;
    }

    /// @return the first byte that follows this view (UTF-8 agnostic) or null character if past enclosing string view.
    ULANG_FORCEINLINE UTF8Char NextByte(const CUTF8StringView& Enclosing) const
    {
        return (_End < Enclosing._End) ? *_End : 0u;
    }

    /// @return the second byte that follows this view (UTF-8 agnostic) or null character if past enclosing string view.
    ULANG_FORCEINLINE UTF8Char NextNextByte(const CUTF8StringView& Enclosing) const
    {
        return ((_End + 1) < Enclosing._End) ? *(_End + 1) : 0u;
    }

    /// @return the first code point in this string (decodes UTF-8)
    ULANG_FORCEINLINE SUniCodePointLength FirstCodePoint() const
    {
        return CUnicode::DecodeUTF8(_Begin, _End - _Begin);
    }

    ULANG_FORCEINLINE bool StartsWith(const CUTF8StringView& Text) const
    {
        for (const UTF8Char *ThisChar = _Begin, *TextChar = Text._Begin; TextChar < Text._End; ++ThisChar, ++TextChar)
        {
            if (ThisChar >= _End || *ThisChar != *TextChar) return false;
        }
        return true;
    }

    ULANG_FORCEINLINE bool EndsWith(const CUTF8StringView& Text) const
    {
        if (Text.ByteLen() > ByteLen())
        {
            return false;
        }

        for (const UTF8Char* ThisChar = _End - Text.ByteLen(), *TextChar = Text._Begin; ThisChar < _End; ++ThisChar, ++TextChar)
        {
            if (*ThisChar != *TextChar) return false;
        }
        return true;
    }

    ULANG_FORCEINLINE int32_t Find(UTF8Char Char) const
    {
        for (const UTF8Char* ThisChar = _Begin; ThisChar < _End; ++ThisChar)
        {
            if (*ThisChar == Char)
            {
                return int32_t(ThisChar - _Begin);
            }
        }
        return IndexNone;
    }

    ULANG_FORCEINLINE int32_t Find(const CUTF8StringView& Text) const
    {
        if (_Begin == _End)
        {
            return IndexNone;
        }
        const UTF8Char* EndChar = _End - Text.ByteLen();    // No need to check further as Text wouldn't fit
        for (const UTF8Char* ThisChar = _Begin; ThisChar <= EndChar; ++ThisChar)
        {
            for (const UTF8Char *SubChar = ThisChar, *TextChar = Text._Begin; TextChar < Text._End; ++TextChar, ++SubChar)
            {
                if (*SubChar != *TextChar)
                {
                    goto Continue;
                }
            }
            return int32_t(ThisChar - _Begin);

        Continue:;
        }

        return IndexNone;
    }

    ULANG_FORCEINLINE bool Contains(UTF8Char Char) const
    {
        return Find(Char) != IndexNone;
    }

    ULANG_FORCEINLINE bool Contains(const CUTF8StringView& Text) const
    {
        return Find(Text) != IndexNone;
    }

    ULANG_FORCEINLINE bool ContainsCaseIndependent(const CUTF8StringView& Text) const
    {
        const UTF8Char* EndChar = _End - Text.ByteLen(); // No need to check further as Text wouldn't fit
        for (const UTF8Char* ThisChar = _Begin; ThisChar <= EndChar; ++ThisChar)
        {
            for (const UTF8Char* SubChar = ThisChar, *TextChar = Text._Begin; TextChar < Text._End; ++TextChar, ++SubChar)
            {
                if (CUnicode::ToLower_ASCII(*SubChar) != CUnicode::ToLower_ASCII(*TextChar))
                {
                    goto Continue;
                }
            }
            return true;

        Continue:;
        }

        return false;
    }

    // Comparisons

    bool operator==(const CUTF8StringView& Other) const
    {
        if (_End - _Begin != Other._End - Other._Begin) return false;
        for (const UTF8Char *ThisChar = _Begin, *OtherChar = Other._Begin; ThisChar < _End; ++ThisChar, ++OtherChar)
        {
            if (*ThisChar != *OtherChar) return false;
        }
        return true;
    }

    bool operator!=(const CUTF8StringView& Other) const
    {
        return !(*this == Other);
    }

    bool operator<(const CUTF8StringView& Other) const
    {
        for (const UTF8Char *ThisChar = _Begin, *OtherChar = Other._Begin; ThisChar < _End && OtherChar < Other._End; ++ThisChar, ++OtherChar)
        {
            if (*ThisChar < *OtherChar) return true;
            if (*ThisChar > *OtherChar) return false;
        }
        return (_End - _Begin < Other._End - Other._Begin);
    }

    bool operator>(const CUTF8StringView& Other) const
    {
        for (const UTF8Char *ThisChar = _Begin, *OtherChar = Other._Begin; ThisChar < _End && OtherChar < Other._End; ++ThisChar, ++OtherChar)
        {
            if (*ThisChar < *OtherChar) return false;
            if (*ThisChar > *OtherChar) return true;
        }
        return (_End - _Begin > Other._End - Other._Begin);
    }

    bool operator<=(const CUTF8StringView& Other) const
    {
        return !(*this > Other);
    }

    bool operator>=(const CUTF8StringView& Other) const
    {
        return !(*this < Other);
    }

    bool operator==(const char* NullTerminatedString) const
    {
        const UTF8Char* OtherChar = (UTF8Char*)NullTerminatedString;
        for (const UTF8Char *ThisChar = _Begin; ThisChar < _End; ++ThisChar, ++OtherChar)
        {
            if (*OtherChar == 0 || *ThisChar != *OtherChar) return false;
        }
        return *OtherChar == 0;
    }

    bool operator!=(const char* NullterminatedString) const
    {
        return !(*this == NullterminatedString);
    }

    bool IsEqualCaseIndependent(const CUTF8StringView& Other) const
    {
        if (_End - _Begin != Other._End - Other._Begin) return false;
        for (const UTF8Char* ThisChar = _Begin, *OtherChar = Other._Begin; ThisChar < _End; ++ThisChar, ++OtherChar)
        {
            if (CUnicode::ToLower_ASCII(*ThisChar) != CUnicode::ToLower_ASCII(*OtherChar)) return false;
        }
        return true;
    }

    // Mutators

    /// @return the first byte in this string (UTF-8 agnostic), and remove it from string
    ULANG_FORCEINLINE UTF8Char PopFirstByte()
    {
        ULANG_ASSERTF(_End > _Begin, "Can't pop front from empty string!");
        return *_Begin++;
    }

    /// @return the first code point in this string (decodes UTF-8), and remove it from string
    ULANG_FORCEINLINE SUniCodePointLength PopFirstCodePoint()
    {
        SUniCodePointLength CodePoint = CUnicode::DecodeUTF8(_Begin, _End - _Begin);
        _Begin += CodePoint._ByteLengthUTF8;
        return CodePoint;
    }

    // Extract sub views

    /// @return the leftmost given number of bytes
    ULANG_FORCEINLINE CUTF8StringView SubViewBegin(int32_t ByteCount) const
    {
        return { _Begin, CMath::Clamp(_Begin + ByteCount, _Begin, _End) };
    }

    /// @return the string to the right of the specified byte location, counting back from the right (end of the string)
    ULANG_FORCEINLINE CUTF8StringView SubViewEnd(int32_t ByteCount) const
    {
        return { CMath::Clamp(_End - ByteCount, _Begin, _End), _End };
    }

    /// @return the string to the right of the specified location, counting forward from the left (from the beginning of the string)
    ULANG_FORCEINLINE CUTF8StringView SubViewTrimBegin(int32_t ByteIndex) const
    {
        return { CMath::Clamp(_Begin + ByteIndex, _Begin, _End), _End };
    }

    /// @return the leftmost bytes from the string chopping the given number of characters from the end
    ULANG_FORCEINLINE CUTF8StringView SubViewTrimEnd(int32_t ByteCount) const
    {
        return { _Begin, CMath::Clamp(_End - ByteCount, _Begin, _End) };
    }

    /// @return the substring from ByteIndex position for ByteCount bytes
    ULANG_FORCEINLINE CUTF8StringView SubView(int32_t ByteIndex, int32_t ByteCount = INT32_MAX) const
    {
        ULANG_ASSERTF(ByteCount >= 0, "ByteCount must be non-negative.");
        const UTF8Char* MidBegin = CMath::Clamp(_Begin + ByteIndex, _Begin, _End);
        const UTF8Char* MidEnd = MidBegin + CMath::Min(ByteCount, int32_t(_End - MidBegin));
        return { MidBegin, MidEnd };
    }

    /// @return a sub view as specified by the range
    ULANG_FORCEINLINE CUTF8StringView SubView(const SIdxRange& Range) const
    {
        ULANG_ASSERTF(Range._Begin <= uint32_t(ByteLen()) && Range._End <= uint32_t(ByteLen()), "Range must be contained in string view.");
        return { _Begin + Range._Begin, _Begin + Range._End };
    }

    /// @return a sub view with both begin and end referring to the byte with the specified index
    ULANG_FORCEINLINE CUTF8StringView SubViewEmpty(int32_t ByteIndex) const
    {
        const UTF8Char* NewBegin = _Begin + ByteIndex;
        ULANG_ASSERTF(ByteIndex >= 0 && NewBegin <= _End, "Index of of Bounds.");
        return { NewBegin, NewBegin };
    }

    /// @return Create index range based on sub view of this view.
    SIdxRange SubRange(const CUTF8StringView& SubView) const
    {
        ULANG_ASSERTF(SubView._Begin >= _Begin && SubView._End <= _End, "Index of of Bounds.");
        return {uint32_t(SubView._Begin - _Begin), uint32_t(SubView._End - _Begin)};
    }

    // Unicode iteration

    class UnicodeConstIterator
    {
    public:
        ULANG_FORCEINLINE UnicodeConstIterator(const UTF8Char* CurrentByte, size_t CurrentByteLen)
            : _CurrentByte(CurrentByte)
            , _CurrentByteLen(CurrentByteLen)
        {
            Eval();
        }

        ULANG_FORCEINLINE UnicodeConstIterator operator++(int)
        {
            UnicodeConstIterator temp(*this);
            ++*this;
            return temp;
        }

        ULANG_FORCEINLINE UnicodeConstIterator &operator++()
        {
            _CurrentByte    += _CurrentValue._ByteLengthUTF8;
            _CurrentByteLen -= _CurrentValue._ByteLengthUTF8;
            Eval();
            return *this;
        }

        ULANG_FORCEINLINE UniCodePoint operator* () const { return _CurrentValue._CodePoint; }

        ULANG_FORCEINLINE bool operator==(const UnicodeConstIterator& Other) const { return _CurrentByte == Other._CurrentByte; }
        ULANG_FORCEINLINE bool operator!=(const UnicodeConstIterator& Other) const { return _CurrentByte != Other._CurrentByte; }
		ULANG_FORCEINLINE const UTF8Char* CurrentByte() const { return this->_CurrentByte; }

    private:

        ULANG_FORCEINLINE void Eval()
        {
            _CurrentValue = _CurrentByteLen > 0 ? CUnicode::DecodeUTF8(_CurrentByte, _CurrentByteLen) : SUniCodePointLength{};
        }

        SUniCodePointLength _CurrentValue;
        const UTF8Char*     _CurrentByte;
        size_t              _CurrentByteLen;
    };

    ULANG_FORCEINLINE UnicodeConstIterator begin() const { return UnicodeConstIterator(_Begin, _End - _Begin); }
    ULANG_FORCEINLINE UnicodeConstIterator end() const { return UnicodeConstIterator(_End, 0); }

protected:

    /*
     * Convert index that may be relative (negative) to the length of the string to direct index and assert if out of bounds.
     * -1=last char, -2=second to last char, etc.
     * @return: Converted direct index
     */
    ULANG_FORCEINLINE int32_t InputByteIdxToDirectIdx(int32_t InIdx) const
    {
        if (InIdx < 0)
        {
            int32_t ByteLen = int32_t(_End - _Begin);
            ULANG_ASSERTF((ByteLen + InIdx) >= 0, "Index `%i` from end of string is out of bounds and resolved to `%i` bytes before the start of the string!", InIdx, -(ByteLen + InIdx));
            return ByteLen + InIdx;
        }

        ULANG_ASSERTF(InIdx < int32_t(_End - _Begin), "Index `%i` is out of bounds in `%i` byte string!", InIdx, int32_t(_End - _Begin));
        return InIdx;
    }

    /*
     * Convert index and span count that may be relative (negative) to the length of the string to direct index and span.
     * Assert if the index is out of bounds.
     * @param: InOutIdx - index to convert. If negative it is relative to the end of the string -1=last char, -2=second to last char, etc.
     * @param: InOutSpan - number of characters in span. If negative it indicates remainder of string after `InOutIdx` - so -1 = include last, -2 = include char before last, etc.
     * @return: true if there is a valid span of characters and false if the span or string is empty
     */
    ULANG_FORCEINLINE bool InputByteIdxSpan(int32_t& InOutIdx, int32_t& InOutSpan) const
    {
        int32_t ByteLen = int32_t(_End - _Begin);

        if ((ByteLen <= 0) || (InOutSpan == 0))
        {
            return false;
        }

        if (InOutIdx < 0)
        {
            ULANG_ASSERTF((ByteLen + InOutIdx) >= 0, "Index `%i` from end of string is out of bounds and resolved to `%i` bytes before the start of the string!", InOutIdx, -(ByteLen + InOutIdx));
            InOutIdx += ByteLen;
        }

        // Permissively allow InOutIdx=ByteLen
        ULANG_ASSERTF(InOutIdx <= ByteLen, "Index `%i` is out of bounds in `%i` byte string!", InOutIdx, ByteLen);

        int32_t CountMax = ByteLen - InOutIdx;

        if (InOutSpan < 0)
        {
            InOutSpan += CountMax + 1;
        }

        // Be forgiving with the count
        InOutSpan = CMath::Clamp(InOutSpan, 0, CountMax);

        return InOutSpan != 0;
    }

    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    friend class TUTF8String;

    template<class OtherAllocatorType, typename... OtherAllocatorArgsType>
    friend class TUTF8StringBuilder;
};

/// Hash function for maps, sets
ULANG_FORCEINLINE uint32_t GetTypeHash(const CUTF8StringView& String) { return CCRC32::Generate(String._Begin, String._End); }

}
