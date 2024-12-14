// Copyright Epic Games, Inc. All Rights Reserved.
// Templates for handling different types of references

#pragma once

#include "uLang/Common/Templates/TypeTraits.h"

namespace uLang
{

//------------------------------------------------------------------
// From RemoveReference.h

/**
 * TRemoveReference<type> will remove any references from a type.
 */
template <typename T> struct TRemoveReference      { using Type = T; };
template <typename T> struct TRemoveReference<T& > { using Type = T; };
template <typename T> struct TRemoveReference<T&&> { using Type = T; };

//------------------------------------------------------------------
// From Decay.h

namespace Private
{
    template <typename T>
    struct TDecayNonReference
    {
        using Type = typename TRemoveCV<T>::Type;
    };

    template <typename T>
    struct TDecayNonReference<T[]>
    {
        using Type = T*;
    };

    template <typename T, uint32_t N>
    struct TDecayNonReference<T[N]>
    {
        using Type = T*;
    };

    template <typename RetType, typename... Params>
    struct TDecayNonReference<RetType(Params...)>
    {
        using Type = RetType (*)(Params...);
    };
}

/**
 * Returns the decayed type of T, meaning it removes all references, qualifiers and
 * applies array-to-pointer and function-to-pointer conversions.
 *
 * http://en.cppreference.com/w/cpp/types/decay
 */
template <typename T>
struct TDecay
{
    using Type = typename Private::TDecayNonReference<typename TRemoveReference<T>::Type>::Type;
};

template <typename T>
using TDecayT = typename TDecay<T>::Type;

//------------------------------------------------------------------
// From UnrealTemplate.h

/**
 * Removes one level of pointer from a type, e.g.:
 *
 * TRemovePointer<      int32  >::Type == int32
 * TRemovePointer<      int32* >::Type == int32
 * TRemovePointer<      int32**>::Type == int32*
 * TRemovePointer<const int32* >::Type == const int32
 */
template <typename T> struct TRemovePointer { typedef T Type; };
template <typename T> struct TRemovePointer<T*> { typedef T Type; };

/**
 * Move will cast a reference to an rvalue reference.
 * This is UE's equivalent of std::move except that it will not compile when passed an rvalue or
 * const object, because we would prefer to be informed when Move will have no effect.
 */
template <typename T>
ULANG_FORCEINLINE typename TRemoveReference<T>::Type&& Move(T&& Obj)
{
    using CastType = typename TRemoveReference<T>::Type;

    // Validate that we're not being passed an rvalue or a const object - the former is redundant, the latter is almost certainly a mistake
    static_assert(TIsLValueReferenceType<T>::Value, "Move called on an rvalue");
    static_assert(!TAreTypesEqual<CastType&, const CastType&>::Value, "Move called on a const object");

    return (CastType&&)Obj;
}

/**
 * MoveIfPossible will cast a reference to an rvalue reference.
 * This is UE's equivalent of std::move.  It doesn't static assert like Move, because it is useful in
 * templates or macros where it's not obvious what the argument is, but you want to take advantage of move semantics
 * where you can but not stop compilation.
 */
template <typename T>
ULANG_FORCEINLINE typename TRemoveReference<T>::Type&& MoveIfPossible(T&& Obj)
{
    using CastType = typename TRemoveReference<T>::Type;
    return (CastType&&)Obj;
}

/**
 * ForwardArg will cast a reference to an rvalue reference.
 * This is UE's equivalent of std::forward.
 */
template <typename T>
ULANG_FORCEINLINE T&& ForwardArg(typename TRemoveReference<T>::Type& Obj)
{
    return (T&&)Obj;
}

template <typename T>
ULANG_FORCEINLINE T&& ForwardArg(typename TRemoveReference<T>::Type&& Obj)
{
    return (T&&)Obj;
}

}