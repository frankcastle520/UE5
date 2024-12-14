// Copyright Epic Games, Inc. All Rights Reserved.
// Templates for invoking functors

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Common/Templates/TypeTraits.h"

namespace uLang
{

//------------------------------------------------------------------
// From Invoke.h

namespace Private
{
    template <typename BaseType, typename CallableType>
    ULANG_FORCEINLINE auto DereferenceIfNecessary(CallableType&& Callable)
        -> typename TEnableIf<TPointerIsConvertibleFromTo<typename TDecay<CallableType>::Type, typename TDecay<BaseType>::Type>::Value, decltype((CallableType&&)Callable)>::Type
    {
        return (CallableType&&)Callable;
    }

    template <typename BaseType, typename CallableType>
    ULANG_FORCEINLINE auto DereferenceIfNecessary(CallableType&& Callable)
        -> typename TEnableIf<!TPointerIsConvertibleFromTo<typename TDecay<CallableType>::Type, typename TDecay<BaseType>::Type>::Value, decltype(*(CallableType&&)Callable)>::Type
    {
        return *(CallableType&&)Callable;
    }
}


/**
 * Invokes a callable with a set of arguments.  Allows the following:
 *
 * - Calling a functor object given a set of arguments.
 * - Calling a function pointer given a set of arguments.
 * - Calling a member function given a reference to an object and a set of arguments.
 * - Calling a member function given a pointer (including smart pointers) to an object and a set of arguments.
 * - Projecting via a data member pointer given a reference to an object.
 * - Projecting via a data member pointer given a pointer (including smart pointers) to an object.
 *
 * See: http://en.cppreference.com/w/cpp/utility/functional/invoke
 */
template <typename FuncType, typename... ArgTypes>
ULANG_FORCEINLINE auto Invoke(FuncType&& Func, ArgTypes&&... Args)
    -> decltype(uLang::ForwardArg<FuncType>(Func)(uLang::ForwardArg<ArgTypes>(Args)...))
{
    return uLang::ForwardArg<FuncType>(Func)(uLang::ForwardArg<ArgTypes>(Args)...);
}

template <typename ReturnType, typename ObjType, typename CallableType>
ULANG_FORCEINLINE auto Invoke(ReturnType ObjType::*pdm, CallableType&& Callable)
    -> decltype(Private::DereferenceIfNecessary<ObjType>(uLang::ForwardArg<CallableType>(Callable)).*pdm)
{
    return Private::DereferenceIfNecessary<ObjType>(uLang::ForwardArg<CallableType>(Callable)).*pdm;
}

template <typename ReturnType, typename ObjType, typename... PMFArgTypes, typename CallableType, typename... ArgTypes>
ULANG_FORCEINLINE auto Invoke(ReturnType (ObjType::*PtrMemFun)(PMFArgTypes...), CallableType&& Callable, ArgTypes&&... Args)
    -> decltype((Private::DereferenceIfNecessary<ObjType>(uLang::ForwardArg<CallableType>(Callable)).*PtrMemFun)(uLang::ForwardArg<ArgTypes>(Args)...))
{
    return (Private::DereferenceIfNecessary<ObjType>(uLang::ForwardArg<CallableType>(Callable)).*PtrMemFun)(uLang::ForwardArg<ArgTypes>(Args)...);
}

template <typename ReturnType, typename ObjType, typename... PMFArgTypes, typename CallableType, typename... ArgTypes>
ULANG_FORCEINLINE auto Invoke(ReturnType (ObjType::*PtrMemFun)(PMFArgTypes...) const, CallableType&& Callable, ArgTypes&&... Args)
    -> decltype((Private::DereferenceIfNecessary<ObjType>(uLang::ForwardArg<CallableType>(Callable)).*PtrMemFun)(uLang::ForwardArg<ArgTypes>(Args)...))
{
    return (Private::DereferenceIfNecessary<ObjType>(uLang::ForwardArg<CallableType>(Callable)).*PtrMemFun)(uLang::ForwardArg<ArgTypes>(Args)...);
}


/**
 * Wraps up a named non-member function so that it can easily be passed as a callable.
 * This allows functions with overloads or default arguments to be treated correctly.
 *
 * Example:
 *
 * TArray<FMyType> Array = ...;
 *
 * // Doesn't compile, because you can't take the address of an overloaded function when its type needs to be deduced.
 * Algo::SortBy(Array, &LexToString);
 *
 * // Works as expected
 * Algo::SortBy(Array, PROJECTION(LexToString));
 */
#define ULANG_PROJECTION(FuncName) \
    [](auto&&... Args) \
    { \
        return FuncName(uLang::ForwardArg<decltype(Args)>(Args)...); \
    }

/**
 * Wraps up a named member function so that it can easily be passed as a callable.
 * This allows functions with overloads or default arguments to be treated correctly.
 *
 * Example:
 *
 * TArray<UObject*> Array = ...;
 *
 * // Doesn't compile, because &UObject::GetFullName loses the default argument and passes
 * // FString (UObject::*)(const UObject*) to Algo::SortBy<>(), which is not a valid projection.
 * Algo::SortBy(Array, &UObject::GetFullName);
 *
 * // Works as expected
 * Algo::SortBy(Array, PROJECTION_MEMBER(UObject, GetFullName));
 */
#define ULANG_PROJECTION_MEMBER(Type, FuncName) \
    [](auto&& Obj, auto&&... Args) \
    { \
        return Private::DereferenceIfNecessary<Type>(uLang::ForwardArg<decltype(Obj)>(Obj)).FuncName(uLang::ForwardArg<decltype(Args)>(Args)...); \
    }

 //------------------------------------------------------------------
 // From IsInvocable.h

namespace Private
{
    template <typename T>
    T&& DeclVal();

    template <typename T>
    struct TVoid
    {
        typedef void Type;
    };

    template <typename, typename CallableType, typename... ArgTypes>
    struct TIsInvocableImpl
    {
        enum { Value = false };
    };

    template <typename CallableType, typename... ArgTypes>
    struct TIsInvocableImpl<typename TVoid<decltype(uLang::Invoke(DeclVal<CallableType>(), DeclVal<ArgTypes>()...))>::Type, CallableType, ArgTypes...>
    {
        enum { Value = true };
    };
}

/**
 * Traits class which tests if an instance of CallableType can be invoked with
 * a list of the arguments of the types provided.
 *
 * Examples:
 *     IsInvocable<void()>::Value == true
 *     IsInvocable<void(), FString>::Value == false
 *     IsInvocable<void(FString), FString>::Value == true
 *     IsInvocable<void(FString), const TCHAR*>::Value == true
 *     IsInvocable<void(FString), int32>::Value == false
 *     IsInvocable<void(char, float, bool), int, int, int>::Value == true
 *     IsInvocable<TFunction<void(FString)>, FString>::Value == true
 *     IsInvocable<TFunction<void(FString)>, int32>::Value == false
 */
template <typename CallableType, typename... ArgTypes>
struct TIsInvocable : Private::TIsInvocableImpl<void, CallableType, ArgTypes...>
{
};

}