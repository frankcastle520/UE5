// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/StaticAssertCompleteType.h"
#include <initializer_list>

/**
 * Traits class which tests if a type is a contiguous container.
 * Requires:
 *    [ &Container[0], &Container[0] + Num ) is a valid range
 */
template <typename T>
struct TIsContiguousContainer
{
	UE_STATIC_ASSERT_COMPLETE_TYPE(T, "TIsContiguousContainer instantiated with an incomplete type");
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
 * Specialization for unbounded C arrays (never contiguous - should be treated as pointers which are not regarded as contiguous containers)
 */
template <typename T> struct TIsContiguousContainer<               T[]> { enum { Value = false }; };
template <typename T> struct TIsContiguousContainer<const          T[]> { enum { Value = false }; };
template <typename T> struct TIsContiguousContainer<      volatile T[]> { enum { Value = false }; };
template <typename T> struct TIsContiguousContainer<const volatile T[]> { enum { Value = false }; };

/**
 * Specialization for initializer lists (also always contiguous)
 */
template <typename T>
struct TIsContiguousContainer<std::initializer_list<T>>
{
	enum { Value = true };
};
