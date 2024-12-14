// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/IsConstructible.h"
#include "Templates/UnrealTemplate.h" // Forward

// Allows simpler form of initializer list such as example below.
// This warning is no longer enabled in GCC's -Wall (for C++ mode), as of 4.8
// See http://gcc.gnu.org/bugzilla/show_bug.cgi?id=25137 for more info.
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif

namespace Verse
{

/* Verse tuple struct template used for accessing native C++ tuples

Usage examples:

Verse::TNativeTuple<int64, double> MultiResultVTuple()
{
	Verse::TNativeTuple<int64, double> TupID = {123, 42.0}; // Create using initializer list
	int64   Elem0 = TupID.Get<0>();          // Copy value
	double& Elem1 = TupID.Get<1>();          // Reference value

	TupID.Get<0>() = 4321;  // In-place element assignment
	Elem1 = 5678.0;        // Alter referenced element

	return {42, 123.0};    // return using initializer list
}
*/

//-------------------------------------------------------------------------------------------------
// Verse native tuple struct - unspecialized
template <typename... Types>
struct TNativeTuple;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helper for tuple element accessor
template <size_t Idx, typename Type>
struct TGetTupleElem;

//-------------------------------------------------------------------------------------------------
// Verse native tuple - empty specialization
template <>
struct TNativeTuple<>
{
};

//-------------------------------------------------------------------------------------------------
// Singly typed tuple - this is important to provide so that sizeof(TNativeTuple<float>) == sizeof(float),
// otherwise if the below tuple definition is used, we get sizeof(TNativeTuple<float>) == sizeof(float) + sizeof(TNativeTuple<>)
template <typename Type>
struct TNativeTuple<Type>
{
	Type First;

	TNativeTuple() = default;

	TNativeTuple(const Type& Arg)
		: First(Arg)
	{
	}

	template <typename TArg, typename = typename TEnableIf<TIsConstructible<Type, TArg&&>::Value>::Type>
	TNativeTuple(TArg&& Arg)
		: First(Forward<TArg>(Arg))
	{
	}

	template <size_t Idx>
	constexpr auto& Get()
	{
		return TGetTupleElem<Idx, TNativeTuple<Type>>::Get(*this);
	}

	template <size_t Idx>
	constexpr const auto& Get() const
	{
		return TGetTupleElem<Idx, TNativeTuple<Type>>::Get(*this);
	}

	friend bool operator==(const TNativeTuple<Type>& Lhs, const TNativeTuple<Type>& Rhs)
	{
		return Lhs.First == Rhs.First;
	}
	friend bool operator!=(const TNativeTuple<Type>& Lhs, const TNativeTuple<Type>& Rhs)
	{
		return !(Lhs == Rhs);
	}
};

//-------------------------------------------------------------------------------------------------
// Recursively defined tuple
template <typename Type, typename... RestTypes>
struct TNativeTuple<Type, RestTypes...>
{
	// Use the Get<idx>() accessors rather than these data members directly - they may change
	// Note that order is important to ensure Elem0, Elem1, ... ElemN
	// It must match the structure layout expected by the BP VM
	Type First;
	TNativeTuple<RestTypes...> Rest;

	TNativeTuple() = default;

	TNativeTuple(const Type& Arg, const RestTypes&... Args)
		: First(Arg)
		, Rest(Args...)
	{
	}

	template <typename TArg, typename... TArgs, typename = typename TEnableIf<sizeof...(RestTypes) == sizeof...(TArgs)>::Type>
	TNativeTuple(TArg&& Arg, TArgs&&... Args)
		: First(Forward<TArg>(Arg))
		, Rest(Forward<TArgs>(Args)...)
	{
	}

	template <typename OtherType, typename... OtherRestTypes, typename = typename TEnableIf<sizeof...(RestTypes) == sizeof...(OtherRestTypes)>::Type>
	TNativeTuple(const TNativeTuple<OtherType, OtherRestTypes...>& Other)
		: First(Other.First)
		, Rest(Other.Rest)
	{
	}

	// Get reference to element 0 .. N - can also be used for assignment
	// Value = MyTuple.Get<0>; MyTuple.Get<3> = NewValue;
	template <size_t Idx>
	constexpr auto& Get()
	{
		return TGetTupleElem<Idx, TNativeTuple<Type, RestTypes...>>::Get(*this);
	}

	// Get const reference to element 0 .. N
	// Value = MyTuple.Get<3>;
	template <size_t Idx>
	constexpr const auto& Get() const
	{
		return TGetTupleElem<Idx, TNativeTuple<Type, RestTypes...>>::Get(*this);
	}

	friend bool operator==(const TNativeTuple<Type, RestTypes...>& Lhs, const TNativeTuple<Type, RestTypes...>& Rhs)
	{
		return Lhs.First == Rhs.First && Lhs.Rest == Rhs.Rest;
	}
	friend bool operator!=(const TNativeTuple<Type, RestTypes...>& Lhs, const TNativeTuple<Type, RestTypes...>& Rhs)
	{
		return !(Lhs == Rhs);
	}
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helper for tuple element accessor - Zeroth specialization
template <typename Type, typename... RestTypes>
struct TGetTupleElem<0, TNativeTuple<Type, RestTypes...>>
{
	constexpr static Type& Get(TNativeTuple<Type, RestTypes...>& Data)
	{
		return Data.First;
	}

	constexpr static const Type& Get(const TNativeTuple<Type, RestTypes...>& Data)
	{
		return Data.First;
	}
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Helper for tuple element accessor - recursive definition
template <size_t Idx, typename Type, typename... RestTypes>
struct TGetTupleElem<Idx, TNativeTuple<Type, RestTypes...>>
{
	constexpr static decltype(auto) Get(TNativeTuple<Type, RestTypes...>& Data)
	{
		return TGetTupleElem<Idx - 1, TNativeTuple<RestTypes...>>::Get(Data.Rest);
	}

	constexpr static decltype(auto) Get(const TNativeTuple<Type, RestTypes...>& Data)
	{
		return TGetTupleElem<Idx - 1, TNativeTuple<RestTypes...>>::Get(Data.Rest);
	}
};

} // namespace Verse
