// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include <intrin.h>

namespace uLang
{
    ULANG_FORCEINLINE uint32_t InterlockedCompareExchange(volatile uint32_t* Value, uint32_t ReplaceWithThis, uint32_t IfEqualToThis)
    {
        return ::_InterlockedCompareExchange((long*)Value, (long)ReplaceWithThis, (long)IfEqualToThis);
    }
    
    ULANG_FORCEINLINE void* InterlockedCompareExchange(void* volatile* Value, void* ReplaceWithThis, void* IfEqualToThis)
    {
        return ::_InterlockedCompareExchangePointer(Value, ReplaceWithThis, IfEqualToThis);
    }
}
