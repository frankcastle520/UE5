// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//------------------------------------------------------------------
// Warnings

#define ULANG_SILENCE_SECURITY_WARNING_START _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wformat-security\"")
#define ULANG_SILENCE_SECURITY_WARNING_END _Pragma("clang diagnostic pop")

//------------------------------------------------------------------
// Debug break

#if defined(__aarch64__)
    #define ULANG_BREAK() __asm__(".inst 0xd4200000")
#elif defined(__arm__)
    #define ULANG_BREAK() __asm__("trap")
#else
    #define ULANG_BREAK() __asm__("int $3")
#endif

namespace uLang
{

//------------------------------------------------------------------
// Check if debugger is present

inline bool IsDebuggerPresent()
{
    return false; // TODO: Implement this
}

//------------------------------------------------------------------
// Send string to debugger output window

inline void LogDebugMessage(const char* Message)
{
    // TODO: Implement this
}

}
