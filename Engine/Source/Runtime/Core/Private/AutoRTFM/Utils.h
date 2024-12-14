// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"

#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAutoRTFM, Display, All)

namespace AutoRTFM
{

[[noreturn]] inline void Unreachable()
{
	LowLevelFatalError(TEXT("Unreachable encountered!"));

#if defined(_MSC_VER) && !defined(__clang__)
    __assume(false);
#else
    __builtin_unreachable();
#endif // PLATFORM_WINDOWS
}

FString GetFunctionDescription(void* FunctionPtr);

template<typename TReturnType, typename... TParameterTypes>
FString GetFunctionDescription(TReturnType (*FunctionPtr)(TParameterTypes...))
{
    return GetFunctionDescription(reinterpret_cast<void*>(FunctionPtr));
}

} // namespace AutoRTFM

#define ASSERT(exp) UE_CLOG(UNLIKELY(!(exp)), LogAutoRTFM, Fatal, TEXT("AutoRTFM assert!"))

#if defined(__has_feature)
	#if __has_feature(address_sanitizer)
		#define AUTORTFM_NO_ASAN [[clang::no_sanitize("address")]]
	#endif
#endif

#if !defined(AUTORTFM_NO_ASAN)
	#define AUTORTFM_NO_ASAN
#endif
