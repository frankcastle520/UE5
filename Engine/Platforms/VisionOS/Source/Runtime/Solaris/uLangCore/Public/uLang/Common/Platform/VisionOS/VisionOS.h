// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//------------------------------------------------------------------
// Debug break

#if defined(ULANG_PLATFORM_IOS_WITH_SIMULATOR) && ULANG_PLATFORM_IOS_WITH_SIMULATOR
	#define ULANG_BREAK() __asm__("int $3")
#elif defined(__LP64__)
	#define ULANG_BREAK() __asm__("svc 0")
#else
	#define ULANG_BREAK() __asm__("trap")
#endif