// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//------------------------------------------------------------------
// Debug break

#if defined(__aarch64__)
    #define ULANG_BREAK() __asm__(".inst 0xd4200000")
#elif defined(__arm__)
    #define ULANG_BREAK() __asm__("trap")
#else
    #define ULANG_BREAK() __asm__("int $3")
#endif
