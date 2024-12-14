// Copyright Epic Games, Inc. All Rights Reserved.
// uLang JOSN support
#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Misc/Optional.h"

ULANG_THIRD_PARTY_INCLUDES_START
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
ULANG_THIRD_PARTY_INCLUDES_END

namespace uLang
{

//====================================================================================
// RapidJSON configuration
//====================================================================================

/**
 * Custom allocator class that routes RapidJSON allocations through the uLang memory interface
 */
class JSONAllocator
{
public:
    static void* Malloc(size_t Size) 
    { 
        return GetSystemParams()._HeapMalloc(Size);
    }
    static void* Realloc(void* OriginalPtr, size_t OriginalSize, size_t NewSize)
    {
        return GetSystemParams()._HeapRealloc(OriginalPtr, NewSize);
    }
    static void Free(void* Ptr)
    {
        GetSystemParams()._HeapFree(Ptr);
    }
};

using JSONMemoryPoolAllocator = rapidjson::MemoryPoolAllocator<JSONAllocator>;
using JSONDocument = rapidjson::GenericDocument<rapidjson::UTF8<char>, JSONMemoryPoolAllocator, JSONAllocator>;
using JSONGenericMemberIterator = rapidjson::GenericMemberIterator<false, rapidjson::UTF8<char>, JSONMemoryPoolAllocator>;
using JSONValue = JSONDocument::ValueType;
using JSONStringBuffer = rapidjson::StringBuffer;
using JSONStringWriter = rapidjson::PrettyWriter<JSONStringBuffer>;
using JSONStringRef = rapidjson::GenericStringRef<char>;

//====================================================================================
// Utility functions
//====================================================================================

/**
 * Given a raw string, return the escaped JSON encoded string (using backslashes)
 */
ULANGJSON_API CUTF8String EscapeJSON(const CUTF8StringView& RawText);

/// @overload
ULANGJSON_API CUTF8String EscapeJSON(const UTF8Char Ch);


//====================================================================================
// JSON -> C++ conversion functions
// Overloads of the function FromJSON for various data types
// Supplement these by adding your own overloads for FromJSON
//====================================================================================

/**
 * Read a bool from JSON
 */
inline bool FromJSON(const JSONValue& JSON, bool* Value)
{
    if (JSON.IsBool())
    {
        *Value = JSON.GetBool();
        return true;
    }
    return false;
}

/**
 * Read an integer from JSON
 */
inline bool FromJSON(const JSONValue& JSON, int* Value)
{
    if (JSON.IsInt())
    {
        *Value = JSON.GetInt();
        return true;
    }
    return false;
}

/**
 * Read an unsigned 32-bit integer from JSON
 */
inline bool FromJSON(const JSONValue& JSON, uint32_t* Value)
{
    if (JSON.IsUint())
    {
        *Value = JSON.GetUint();
        return true;
    }
    return false;
}

/**
 * Read an unsigned 64-bit integer from JSON
 */
inline bool FromJSON(const JSONValue& JSON, uint64_t* Value)
{
    if (JSON.IsUint64())
    {
        *Value = JSON.GetUint64();
        return true;
    }
    return false;
}

/**
 * Read a string from JSON
 */
inline bool FromJSON(const JSONValue& JSON, CUTF8String* Value)
{
    if (JSON.IsString())
    {
        *Value = CUTF8StringView(JSON.GetString(), JSON.GetStringLength());
        return true;
    }
    return false;
}

inline bool FromJSON(const JSONValue& JSON, CUTF8StringView* Value)
{
    if (JSON.IsString())
    {
        *Value = CUTF8StringView(JSON.GetString(), JSON.GetStringLength());
        return true;
    }
    return false;
}

/**
 * Read an optional from JSON (which can be null meaning it's unset)
 */
template<class T>
bool FromJSON(const JSONValue& JSON, TOptional<T>* OptionalValue)
{
    if (JSON.IsNull())
    {
        *OptionalValue = EResult::Unspecified;
        return true;
    }

    T Value;
    if (FromJSON(JSON, &Value))
    {
        *OptionalValue = Value;
        return true;
    }

    return false;
}

/**
 * Read an array from JSON
 */
template<class T>
bool FromJSON(const JSONValue& JSON, TArray<T>* ArrayValue)
{
    if (JSON.IsArray())
    {
        ArrayValue->SetNum(JSON.Size());
        for (uint32_t i = 0; i < JSON.Size(); ++i)
        {
            if (!FromJSON(JSON[i], &(*ArrayValue)[i]))
            {
                return false;
            }
        }
        return true;
    }

    return false;
}

/**
 * Read member of a JSON object
 */
template<class T>
bool FromJSON(const JSONValue& JSON, const char* MemberName, T* MemberValue, TOptional<bool> bRequired = EResult::Unspecified)
{
    if (!JSON.IsObject())
    {
        return false;
    }

    auto Member = JSON.FindMember(MemberName);
    if (Member != JSON.MemberEnd())
    {
        return FromJSON(Member->value, MemberValue);
    }

    // Fail if required or if T wasn't optional
    return bRequired ? !*bRequired : bool(TIsOptional<typename TRemovePointer<T>::Type>::Value);
}

/**
 * Write a bool to JSON
 */
inline bool ToJSON(bool Value, JSONValue* JSON, JSONMemoryPoolAllocator&)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetBool(Value);
    return true;
}

/**
 * Write an integer to JSON
 */
inline bool ToJSON(int Value, JSONValue* JSON, JSONMemoryPoolAllocator&)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetInt(Value);
    return true;
}

/**
 * Write an unsigned integer to JSON
 */
inline bool ToJSON(uint32_t Value, JSONValue* JSON, JSONMemoryPoolAllocator&)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetUint(Value);
    return true;
}

/**
 * Write a string to JSON
 */
inline bool ToJSON(CUTF8StringView Value, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetString((const char*)Value._Begin, Value.ByteLen(), Allocator);
    return true;
}

/**
 * Write an optional to JSON (which can be null meaning it's unset)
 */
template<class T>
bool ToJSON(const TOptional<T>& OptionalValue, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!OptionalValue.IsSet())
    {
        return true;
    }

    if (ToJSON(*OptionalValue, JSON, Allocator))
    {
        return true;
    }

    return false;
}

/**
 * Write an array to JSON
 */
template<class T>
bool ToJSON(const TArray<T>& ArrayValue, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSON->SetArray();
    JSON->Reserve(ArrayValue.Num(), Allocator);

    for (const T& i : ArrayValue)
    {
        JSONValue Elem;
        if (!ToJSON(i, &Elem, Allocator))
        {
            return false;
        }

        JSON->PushBack(Elem, Allocator);
    }

    return true;
}

/**
 * Write member of a JSON object
 */
template<class T>
bool ToJSON(const T& MemberValue, const char* MemberName, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    JSONValue Member;
    if (!ToJSON(MemberValue, &Member, Allocator))
    {
        return false;
    }

    JSON->AddMember(JSONStringRef(MemberName), Member, Allocator);

    return true;
}

/**
 * Write optional member of a JSON object
 */
template<class T>
bool ToJSON(const TOptional<T>& MemberValue, const char* MemberName, JSONValue* JSON, JSONMemoryPoolAllocator& Allocator)
{
    if (!JSON)
    {
        return false;
    }

    if (!MemberValue.IsSet())
    {
        return true;
    }

    return ToJSON(MemberValue.GetValue(), MemberName, JSON, Allocator);
}

} // namespace uLang
