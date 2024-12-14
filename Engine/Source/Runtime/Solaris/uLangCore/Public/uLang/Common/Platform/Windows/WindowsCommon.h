// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

extern "C" { __declspec(dllimport) int __stdcall IsDebuggerPresent(); }
extern "C" { __declspec(dllimport) void __stdcall OutputDebugStringA(_In_opt_ const char*); }

//------------------------------------------------------------------
// Warnings

#if defined(__clang__)
#define ULANG_SILENCE_SECURITY_WARNING_START \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wformat-security\"")
#define ULANG_SILENCE_SECURITY_WARNING_END \
    _Pragma("clang diagnostic pop")
#endif

//------------------------------------------------------------------
// Debug break

#define ULANG_BREAK() __debugbreak()

namespace uLang
{

//------------------------------------------------------------------
// Check if debugger is present

inline bool IsDebuggerPresent()
{
    return !!::IsDebuggerPresent();
}

//------------------------------------------------------------------
// Send string to debugger output window

inline void LogDebugMessage(const char* Message)
{
    ::OutputDebugStringA(Message);
}

}
