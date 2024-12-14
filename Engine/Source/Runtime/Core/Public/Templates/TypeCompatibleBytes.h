// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreTypes.h"
#include <string.h>
#include <type_traits>

/**
 * Used to declare an untyped array of data with compile-time alignment.
 * It needs to use template specialization as the MS_ALIGN and GCC_ALIGN macros require literal parameters.
 */
template<int32 Size, uint32 Alignment>
struct TAlignedBytes
{
	alignas(Alignment) uint8 Pad[Size];
};

/** An untyped array of data with compile-time alignment and size derived from another type. */
template<typename ElementType>
struct TTypeCompatibleBytes
{
	using ElementTypeAlias_NatVisHelper = ElementType;
	ElementType*		GetTypedPtr()		{ return (ElementType*)this;  }
	const ElementType*	GetTypedPtr() const	{ return (const ElementType*)this; }

	alignas(ElementType) uint8 Pad[sizeof(ElementType)];
};

template <
	typename ToType,
	typename FromType,
	std::enable_if_t<sizeof(ToType) == sizeof(FromType) && std::is_trivially_copyable_v<ToType> && std::is_trivially_copyable_v<FromType>>* = nullptr
>
inline ToType BitCast(const FromType& From)
{
// Ensure we can use this builtin - seems to be present on Clang 9, GCC 11 and MSVC 19.26,
// but gives spurious "non-void function 'BitCast' should return a value" errors on some
// Mac and Android toolchains when building PCHs, so avoid those.
// However, there is a bug in the Clang static analyzer with this builtin: https://github.com/llvm/llvm-project/issues/69922
// Don't use it when performing static analysis until the bug is fixed.
#if !defined(__clang_analyzer__) && PLATFORM_COMPILER_SUPPORTS_BUILTIN_BITCAST // can consider replacing with __has_builtin(__builtin_bit_cast) once there's no special cases
	return __builtin_bit_cast(ToType, From);
#else
	TTypeCompatibleBytes<ToType> Result;
	memcpy(&Result, &From, sizeof(ToType));
	return *Result.GetTypedPtr();
#endif
}
