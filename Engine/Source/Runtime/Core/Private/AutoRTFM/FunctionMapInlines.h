// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFMConstants.h"
#include "Context.h"
#include "FunctionMap.h"
#include "Utils.h"

#include "Containers/StringConv.h"

namespace AutoRTFM
{

inline void* FunctionMapLookup(void* OldFunction, const char* Where)
{
	// We use prefix data in our custom LLVM pass to stuff some data just
	// before the address of all open function pointers (that we have
	// definitions for!). We use the special Magic Mike constant in the top
	// 16-bits of the function pointer address as a magic constant check
	// to give us a much higher confidence that there is actually a closed
	// variant pointer residing 8-bytes before our function address.
	const uint64 PrefixData = *(reinterpret_cast<uint64*>(OldFunction) - 1);

	if (LIKELY(Constants::MagicMike == (PrefixData & 0xffff000000000000)))
	{
		return reinterpret_cast<void*>(PrefixData & 0x0000ffffffffffff);
	}

	// Instead fall back to the slower function map lookup.
    void* const Result = FunctionMapTryLookup(OldFunction);

	if (UNLIKELY(!Result))
	{
#ifdef __clang__
		[[clang::musttail]]
#endif
		return FunctionMapReportError(OldFunction, Where);
	}

	return Result;
}

template<typename TReturnType, typename... TParameterTypes>
auto FunctionMapLookup(TReturnType (*Function)(TParameterTypes...), const char* Where) -> TReturnType (*)(TParameterTypes...)
{
    return reinterpret_cast<TReturnType (*)(TParameterTypes...)>(FunctionMapLookup(reinterpret_cast<void*>(Function), Where));
}

} // namespace AutoRTFM

