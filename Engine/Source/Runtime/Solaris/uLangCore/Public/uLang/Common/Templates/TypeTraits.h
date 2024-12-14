// Copyright Epic Games, Inc. All Rights Reserved.
// Templates for determining properties/traits of types

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Templates/Conditionals.h"
#include <initializer_list>
#include <type_traits>

namespace uLang
{

//------------------------------------------------------------------
// From RemoveCV.h

/**
 * TRemoveCV<type> will remove any const/volatile qualifiers from a type.
 * (based on std::remove_cv<>
 * note: won't remove the const from "const int*", as the pointer is not const
 */
template <typename T> struct TRemoveCV                   { using Type = T; };
template <typename T> struct TRemoveCV<const T>          { using Type = T; };
template <typename T> struct TRemoveCV<volatile T>       { using Type = T; };
template <typename T> struct TRemoveCV<const volatile T> { using Type = T; };

//------------------------------------------------------------------
// From AreTypesEqual.h

/** Tests whether two typenames refer to the same type. */
template<typename A, typename B>
struct TAreTypesEqual;

template<typename, typename>
struct TAreTypesEqual
{
    enum { Value = false };
};

template<typename A>
struct TAreTypesEqual<A, A>
{
    enum { Value = true };
};

//------------------------------------------------------------------
// From IsEnum.h

template <typename T>
struct TIsEnum
{
    enum { Value = __is_enum(T) };
};

//------------------------------------------------------------------
// From IsArithmetic.h

/**
 * Traits class which tests if a type is arithmetic.
 */
template <typename T>
struct TIsArithmetic
{
    enum { Value = false };
};

template <> struct TIsArithmetic<float>       { enum { Value = true }; };
template <> struct TIsArithmetic<double>      { enum { Value = true }; };
template <> struct TIsArithmetic<long double> { enum { Value = true }; };
template <> struct TIsArithmetic<uint8_t>     { enum { Value = true }; };
template <> struct TIsArithmetic<uint16_t>    { enum { Value = true }; };
template <> struct TIsArithmetic<uint32_t>    { enum { Value = true }; };
template <> struct TIsArithmetic<uint64_t>    { enum { Value = true }; };
template <> struct TIsArithmetic<int8_t>      { enum { Value = true }; };
template <> struct TIsArithmetic<int16_t>     { enum { Value = true }; };
template <> struct TIsArithmetic<int32_t>     { enum { Value = true }; };
template <> struct TIsArithmetic<int64_t>     { enum { Value = true }; };
template <> struct TIsArithmetic<bool>        { enum { Value = true }; };
template <> struct TIsArithmetic<char>        { enum { Value = true }; };
template <> struct TIsArithmetic<wchar_t>     { enum { Value = true }; };

template <typename T> struct TIsArithmetic<const          T> { enum { Value = TIsArithmetic<T>::Value }; };
template <typename T> struct TIsArithmetic<      volatile T> { enum { Value = TIsArithmetic<T>::Value }; };
template <typename T> struct TIsArithmetic<const volatile T> { enum { Value = TIsArithmetic<T>::Value }; };

//------------------------------------------------------------------
// From IsIntegral.h

/**
 * Traits class which tests if a type is integral.
 */
template <typename T>
struct TIsIntegral
{
    enum { Value = false };
};

template <> struct TIsIntegral<         bool>      { enum { Value = true }; };
template <> struct TIsIntegral<         char>      { enum { Value = true }; };
template <> struct TIsIntegral<signed   char>      { enum { Value = true }; };
template <> struct TIsIntegral<unsigned char>      { enum { Value = true }; };
template <> struct TIsIntegral<         char16_t>  { enum { Value = true }; };
template <> struct TIsIntegral<         char32_t>  { enum { Value = true }; };
template <> struct TIsIntegral<         wchar_t>   { enum { Value = true }; };
template <> struct TIsIntegral<         short>     { enum { Value = true }; };
template <> struct TIsIntegral<unsigned short>     { enum { Value = true }; };
template <> struct TIsIntegral<         int>       { enum { Value = true }; };
template <> struct TIsIntegral<unsigned int>       { enum { Value = true }; };
template <> struct TIsIntegral<         long>      { enum { Value = true }; };
template <> struct TIsIntegral<unsigned long>      { enum { Value = true }; };
template <> struct TIsIntegral<         long long> { enum { Value = true }; };
template <> struct TIsIntegral<unsigned long long> { enum { Value = true }; };

template <typename T> struct TIsIntegral<const          T> { enum { Value = TIsIntegral<T>::Value }; };
template <typename T> struct TIsIntegral<      volatile T> { enum { Value = TIsIntegral<T>::Value }; };
template <typename T> struct TIsIntegral<const volatile T> { enum { Value = TIsIntegral<T>::Value }; };

//------------------------------------------------------------------
// From IsPointer.h

/**
 * Traits class which tests if a type is a pointer.
 */
template <typename T>
struct TIsPointer
{
    enum { Value = false };
};

template <typename T> struct TIsPointer<T*> { enum { Value = true }; };

template <typename T> struct TIsPointer<const          T> { enum { Value = TIsPointer<T>::Value }; };
template <typename T> struct TIsPointer<      volatile T> { enum { Value = TIsPointer<T>::Value }; };
template <typename T> struct TIsPointer<const volatile T> { enum { Value = TIsPointer<T>::Value }; };

//------------------------------------------------------------------
// From IsMemberPointer.h

/**
 * Traits class which tests if a type is a pointer to member (data member or member function).
 */
template <typename T>
struct TIsMemberPointer
{
    enum { Value = false };
};

template <typename T, typename U> struct TIsMemberPointer<T U::*> { enum { Value = true }; };

template <typename T> struct TIsMemberPointer<const          T> { enum { Value = TIsPointer<T>::Value }; };
template <typename T> struct TIsMemberPointer<      volatile T> { enum { Value = TIsPointer<T>::Value }; };
template <typename T> struct TIsMemberPointer<const volatile T> { enum { Value = TIsPointer<T>::Value }; };

//------------------------------------------------------------------
// From IsPODType.h

/**
 * Traits class which tests if a type is POD.
 */

#if defined(_MSC_VER) && _MSC_VER >= 1900
    // __is_pod changed in VS2015, however the results are still correct for all usages I've been able to locate.
    #pragma warning(push)
    #pragma warning(disable:4647)
#endif // _MSC_VER == 1900

template <typename T>
struct TIsPODType
{
    enum { Value = TOrValue<__is_pod(T) || __is_enum(T), TIsArithmetic<T>, TIsPointer<T>>::Value };
};

#if defined(_MSC_VER) && _MSC_VER >= 1900
    #pragma warning(pop)
#endif // _MSC_VER >= 1900

//------------------------------------------------------------------
// From PointerIsConvertibleFromTo.h

/**
 * Tests if a From* is convertible to a To*
 **/
template <typename From, typename To>
struct TPointerIsConvertibleFromTo
{
private:
    static uint8_t  Test(...);
    static uint16_t Test(To*);

public:
    enum { Value = sizeof(Test((From*)nullptr)) - 1 };
};

template <typename T1, typename T2>
struct TPointerIsStaticCastableFromTo
{
    enum { Value = static_cast<size_t>(TPointerIsConvertibleFromTo<T1, T2>::Value) | static_cast<size_t>(TPointerIsConvertibleFromTo<T2, T1>::Value) };
};

class TPointerIsConvertibleFromTo_TestBase
{
};

class TPointerIsConvertibleFromTo_TestDerived : public TPointerIsConvertibleFromTo_TestBase
{
};

class TPointerIsConvertibleFromTo_Unrelated
{
};

static_assert(TPointerIsConvertibleFromTo<bool, bool>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<void, void>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<bool, void>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<const bool, const void>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_TestDerived, TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_TestDerived, const TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<const TPointerIsConvertibleFromTo_TestDerived, const TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_TestBase, TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_TestBase, void>::Value, "Platform TPointerIsConvertibleFromTo test failed.");

static_assert(!TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_TestBase, TPointerIsConvertibleFromTo_TestDerived>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(!TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_Unrelated, TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(!TPointerIsConvertibleFromTo<bool, TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(!TPointerIsConvertibleFromTo<void, TPointerIsConvertibleFromTo_TestBase>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(!TPointerIsConvertibleFromTo<TPointerIsConvertibleFromTo_TestBase, bool>::Value, "Platform TPointerIsConvertibleFromTo test failed.");
static_assert(!TPointerIsConvertibleFromTo<void, bool>::Value, "Platform TPointerIsConvertibleFromTo test failed.");

//------------------------------------------------------------------
// From IsConstructible.h

/**
 * Determines if T is constructible from a set of arguments.
 */
template <typename T, typename... Args>
struct TIsConstructible
{
    enum { Value = __is_constructible(T, Args...) };
};

//------------------------------------------------------------------
// From IsTriviallyDestructible.h

/**
 * Traits class which tests if a type has a trivial destructor.
 */
template <typename T>
struct TIsTriviallyDestructible
{
    enum { Value = std::is_trivially_destructible_v<T> };
};

//------------------------------------------------------------------
// From IsContiguousContainer.h

/**
 * Traits class which tests if a type is a contiguous container.
 * Requires:
 *    [ &Container[0], &Container[0] + Num ) is a valid range
 */
template <typename T>
struct TIsContiguousContainer
{
    enum { Value = false };
};

template <typename T> struct TIsContiguousContainer<             T& > : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<             T&&> : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<const          T> : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<      volatile T> : TIsContiguousContainer<T> {};
template <typename T> struct TIsContiguousContainer<const volatile T> : TIsContiguousContainer<T> {};

/**
 * Specialization for C arrays (always contiguous)
 */
template <typename T, size_t N> struct TIsContiguousContainer<               T[N]> { enum { Value = true }; };
template <typename T, size_t N> struct TIsContiguousContainer<const          T[N]> { enum { Value = true }; };
template <typename T, size_t N> struct TIsContiguousContainer<      volatile T[N]> { enum { Value = true }; };
template <typename T, size_t N> struct TIsContiguousContainer<const volatile T[N]> { enum { Value = true }; };

/**
 * Specialization for initializer lists (also always contiguous)
 */
template <typename T>
struct TIsContiguousContainer<std::initializer_list<T>>
{
    enum { Value = true };
};

//------------------------------------------------------------------
// From UnrealTypeTraits.h

/**
 * TIsSame
 *
 * Unreal implementation of std::is_same trait.
 */
template<typename A, typename B>    struct TIsSame { enum { Value = false }; };
template<typename T>                struct TIsSame<T, T> { enum { Value = true }; };

/**
 * TIsReferenceType
 */
template<typename T> struct TIsReferenceType      { enum { Value = false }; };
template<typename T> struct TIsReferenceType<T&>  { enum { Value = true }; };
template<typename T> struct TIsReferenceType<T&&> { enum { Value = true }; };

/**
 * TIsLValueReferenceType
 */
template<typename T> struct TIsLValueReferenceType     { enum { Value = false }; };
template<typename T> struct TIsLValueReferenceType<T&> { enum { Value = true }; };

/**
 * TIsRValueReferenceType
 */
template<typename T> struct TIsRValueReferenceType      { enum { Value = false }; };
template<typename T> struct TIsRValueReferenceType<T&&> { enum { Value = true }; };

/**
 * TIsZeroConstructType
 */
template<typename T>
struct TIsZeroConstructType
{
    enum { Value = TOr<TIsEnum<T>, TIsArithmetic<T>, TIsPointer<T>>::Value };
};

/*-----------------------------------------------------------------------------
    Call traits - Modeled somewhat after boost's interfaces.
-----------------------------------------------------------------------------*/

/**
 * Call traits helpers
 */
template <typename T, bool TypeIsSmall>
struct TCallTraitsParamTypeHelper
{
    using ParamType = const T&;
    using ConstParamType = const T&;
};
template <typename T>
struct TCallTraitsParamTypeHelper<T, true>
{
    using ParamType = const T;
    using ConstParamType = const T;
};
template <typename T>
struct TCallTraitsParamTypeHelper<T*, true>
{
    using ParamType = T*;
    using ConstParamType = const T*;
};

/*-----------------------------------------------------------------------------
 * TCallTraits
 *
 * Same call traits as boost, though not with as complete a solution.
 *
 * The main member to note is ParamType, which specifies the optimal
 * form to pass the type as a parameter to a function.
 *
 * Has a small-value optimization when a type is a POD type and as small as a pointer.
-----------------------------------------------------------------------------*/

/**
 * base class for call traits. Used to more easily refine portions when specializing
 */
template <typename T>
struct TCallTraitsBase
{
private:
    enum { PassByValue = TOr<TAndValue<(sizeof(T) <= sizeof(void*)), TIsPODType<T>>, TIsArithmetic<T>, TIsPointer<T>>::Value };
public:
    using ValueType = T;
    using Reference = T&;
    using ConstReference = const T&;
    using ParamType = typename TCallTraitsParamTypeHelper<T, PassByValue>::ParamType;
    using ConstPointerType = typename TCallTraitsParamTypeHelper<T, PassByValue>::ConstParamType;
};

/**
 * TCallTraits
 */
template <typename T>
struct TCallTraits : public TCallTraitsBase<T> {};

// Fix reference-to-reference problems.
template <typename T>
struct TCallTraits<T&>
{
    using ValueType = T&;
    using Reference = T&;
    using ConstReference = const T&;
    using ParamType = T&;
    using ConstPointerType = T&;
};

// Array types
template <typename T, size_t N>
struct TCallTraits<T [N]>
{
private:
    using ArrayType = T[N];
public:
    using ValueType = const T*;
    using Reference = ArrayType&;
    using ConstReference = const ArrayType&;
    using ParamType = const T* const;
    using ConstPointerType = const T* const;
};

// const array types
template <typename T, size_t N>
struct TCallTraits<const T [N]>
{
private:
    using ArrayType = T[N];
public:
    using ValueType = const T*;
    using Reference = ArrayType&;
    using ConstReference = const ArrayType&;
    using ParamType = const T* const;
    using ConstPointerType = const T* const;
};

/**
 * Helper for array traits. Provides a common base to more easily refine a portion of the traits
 * when specializing. Mainly used by MemoryOps.h which is used by the contiguous storage containers like TArray.
 */
template<typename T>
struct TTypeTraitsBase
{
    using ConstInitType = typename TCallTraits<T>::ParamType;
    using ConstPointerType = typename TCallTraits<T>::ConstPointerType;

    // There's no good way of detecting this so we'll just assume it to be true for certain known types and expect
    // users to customize it for their custom types.
    enum { IsBytewiseComparable = TOr<TIsEnum<T>, TIsArithmetic<T>, TIsPointer<T>>::Value };
};

/**
 * Traits for types.
 */
template<typename T> struct TTypeTraits : public TTypeTraitsBase<T> {};

/**
 * Traits for containers.
 */
template<typename T> struct TContainerTraitsBase
{
    // This should be overridden by every container that supports emptying its contents via a move operation.
    enum { MoveWillEmptyContainer = false };
};

template<typename T> struct TContainerTraits : public TContainerTraitsBase<T> {};

/**
 * Tests if a type T is bitwise-constructible from a given argument type U.  That is, whether or not
 * the U can be memcpy'd in order to produce an instance of T, rather than having to go
 * via a constructor.
 *
 * Examples:
 * TIsBitwiseConstructible<PODType,    PODType   >::Value == true  // PODs can be trivially copied
 * TIsBitwiseConstructible<const int*, int*      >::Value == true  // a non-const Derived pointer is trivially copyable as a const Base pointer
 * TIsBitwiseConstructible<int*,       const int*>::Value == false // not legal the other way because it would be a const-correctness violation
 * TIsBitwiseConstructible<int32_t,    uint32_t  >::Value == true  // signed integers can be memcpy'd as unsigned integers
 * TIsBitwiseConstructible<uint32_t,   int32_t   >::Value == true  // and vice versa
 */

template <typename T, typename Arg>
struct TIsBitwiseConstructible
{
    static_assert(
        !TIsReferenceType<T  >::Value &&
        !TIsReferenceType<Arg>::Value,
        "TIsBitwiseConstructible is not designed to accept reference types");

    static_assert(
        TAreTypesEqual<T,   typename TRemoveCV<T  >::Type>::Value &&
        TAreTypesEqual<Arg, typename TRemoveCV<Arg>::Type>::Value,
        "TIsBitwiseConstructible is not designed to accept qualified types");

    // Assume no bitwise construction in general
    enum { Value = false };
};

template <typename T>
struct TIsBitwiseConstructible<T, T>
{
    // Ts can always be bitwise constructed from itself if it is trivially copyable.
    enum { Value = std::is_trivially_copy_constructible_v<T> };
};

template <typename T, typename U>
struct TIsBitwiseConstructible<const T, U> : TIsBitwiseConstructible<T, U>
{
    // Constructing a const T is the same as constructing a T
};

// Const pointers can be bitwise constructed from non-const pointers.
// This is not true for pointer conversions in general, e.g. where an offset may need to be applied in the case
// of multiple inheritance, but there is no way of detecting that at compile-time.
template <typename T>
struct TIsBitwiseConstructible<const T*, T*>
{
    // Constructing a const T is the same as constructing a T
    enum { Value = true };
};

// Unsigned types can be bitwise converted to their signed equivalents, and vice versa.
// (assuming two's-complement, which we are)
template <> struct TIsBitwiseConstructible< uint8_t,   int8_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible<  int8_t,  uint8_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible<uint16_t,  int16_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible< int16_t, uint16_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible<uint32_t,  int32_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible< int32_t, uint32_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible<uint64_t,  int64_t> { enum { Value = true }; };
template <> struct TIsBitwiseConstructible< int64_t, uint64_t> { enum { Value = true }; };

}
