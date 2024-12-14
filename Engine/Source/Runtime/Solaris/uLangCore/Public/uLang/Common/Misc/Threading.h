// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

#if __has_include(ULANG_PLATFORM_HEADER_NAME_WITH_PREFIX(uLang/Common/Platform,Threading.h))

    // Allow the platform to provide threading implementation.
    #include ULANG_PLATFORM_HEADER_NAME_WITH_PREFIX(uLang/Common/Platform,Threading.h)

#elif defined(__clang__) || defined(__GNUC__)

    // Generic implementation for GCC/Clang compilers
    namespace uLang
    {
        ULANG_FORCEINLINE uint32_t InterlockedCompareExchange(volatile uint32_t* Value, uint32_t ReplaceWithThis, uint32_t IfEqualToThis)
        {
            return __sync_val_compare_and_swap(Value, IfEqualToThis, ReplaceWithThis);
        }

        ULANG_FORCEINLINE void* InterlockedCompareExchange(void* volatile* Value, void* ReplaceWithThis, void* IfEqualToThis)
        {
            return __sync_val_compare_and_swap(Value, IfEqualToThis, ReplaceWithThis);
        }
    }

#else

    #error No platform Threading.h header provided.

#endif
