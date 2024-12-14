// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Templates/TypeTraits.h"

namespace uLang
{

// Abstract archive base class, similar to UE FArchive
// Assumes same endian on all platforms
class CArchive
{
public:
    CArchive() = default;
    ~CArchive() = default;

    CArchive(const CArchive&) = delete;
    CArchive& operator=(const CArchive& ArchiveToCopy) = delete;

    // Whether this archive is for loading data
    ULANG_FORCEINLINE bool IsLoading() const { return _bIsLoading; }

    // The core serialization function - called for serialization of anything
    virtual void Serialize(void* Data, int64_t NumBytes) = 0;

    // Convenience operator for serializing integers and floats
    template<class T>
    ULANG_FORCEINLINE friend typename TEnableIf<TIsArithmetic<T>::Value, CArchive&>::Type operator<<(CArchive& Ar, T& Value) { Ar.Serialize(&Value, sizeof(Value)); return Ar; }

    // Convenience operator for serializing a string
    ULANG_FORCEINLINE friend CArchive& operator<<(CArchive& Ar, CUTF8String& Value)
    {
        int32_t ByteLength = Value.ByteLen();
        Ar << ByteLength;
        if (Ar.IsLoading())
        {
            Value = CUTF8String(ByteLength, [&Ar, ByteLength](UTF8Char* Memory) { Ar.Serialize(Memory, ByteLength); });
        }
        else
        {
            Ar.Serialize((void*)Value.AsUTF8(), Value.ByteLen());
        }
        return Ar;
    }

    // Convenience operator for serializing an optional
    template<class T>
    ULANG_FORCEINLINE friend CArchive& operator<<(CArchive& Ar, TOptional<T>& Value)
    {
        EResult Result = Value.GetResult();
        Ar << (int8_t&)Result;
        if (Result == EResult::OK)
        {
            if (Ar.IsLoading())
            {
                Value.Emplace();
            }
            Ar << *Value;
        }
        return Ar;
    }

    // Convenience operator for serializing an array
    template<class ElementType>
    ULANG_FORCEINLINE friend CArchive& operator<<(CArchive& Ar, TArray<ElementType>& Value)
    {
        int32_t NumElements = Value.Num();
        Ar << NumElements;
        if (Ar.IsLoading())
        {
            Value.SetNum(NumElements);
        }
        for (ElementType& Element : Value)
        {
            Ar << Element;
        }
        return Ar;
    }

protected:

    bool _bIsLoading = false;

};

}
