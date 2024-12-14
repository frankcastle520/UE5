// Copyright Epic Games, Inc. All Rights Reserved.
// Templates for conditional flow control inside other templates

#pragma once

namespace uLang
{

//------------------------------------------------------------------
// From AndOrNot.h

/**
 * Does a boolean AND of the ::Value static members of each type, but short-circuits if any Type::Value == false.
 */
template <typename... Types>
struct TAnd;

template <bool LHSValue, typename... RHS>
struct TAndValue
{
    enum { Value = TAnd<RHS...>::Value };
};

template <typename... RHS>
struct TAndValue<false, RHS...>
{
    enum { Value = false };
};

template <typename LHS, typename... RHS>
struct TAnd<LHS, RHS...> : TAndValue<LHS::Value, RHS...>
{
};

template <>
struct TAnd<>
{
    enum { Value = true };
};

/**
 * Does a boolean OR of the ::Value static members of each type, but short-circuits if any Type::Value == true.
 */
template <typename... Types>
struct TOr;

template <bool LHSValue, typename... RHS>
struct TOrValue
{
    enum { Value = TOr<RHS...>::Value };
};

template <typename... RHS>
struct TOrValue<true, RHS...>
{
    enum { Value = true };
};

template <typename LHS, typename... RHS>
struct TOr<LHS, RHS...> : TOrValue<LHS::Value, RHS...>
{
};

template <>
struct TOr<>
{
    enum { Value = false };
};

/**
 * Does a boolean NOT of the ::Value static members of the type.
 */
template <typename Type>
struct TNot
{
    enum { Value = !Type::Value };
};

//------------------------------------------------------------------
// From EnableIf.h

/**
 * Includes a function in an overload set if the predicate is true.  It should be used similarly to this:
 *
 * // This function will only be instantiated if SomeTrait<T>::Value is true for a particular T
 * template <typename T>
 * typename TEnableIf<SomeTrait<T>::Value, ReturnType>::Type Function(const T& Obj)
 * {
 *     ...
 * }
 *
 * ReturnType is the real return type of the function.
 */
template <bool Predicate, typename Result = void>
class TEnableIf;

template <typename Result>
class TEnableIf<true, Result>
{
public:
    using Type = Result;
};

template <typename Result>
class TEnableIf<false, Result>
{};

template <bool Predicate, typename Result = void>
using TEnableIfT = typename TEnableIf<Predicate, Result>::Type;

//------------------------------------------------------------------
// From ChooseClass.h

/** Chooses between two different classes based on a boolean. */
template<bool Predicate, typename TrueClass, typename FalseClass>
class TChooseClass;

template<typename TrueClass, typename FalseClass>
class TChooseClass<true, TrueClass, FalseClass>
{
public:
    using Result = TrueClass;
};

template<typename TrueClass, typename FalseClass>
class TChooseClass<false, TrueClass, FalseClass>
{
public:
    using Result = FalseClass;
};

}