// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMUnreachable.h"

namespace Verse
{

inline void EnsureStillAliveHere(const void* Pointer)
{
#if PLATFORM_COMPILER_CLANG
	asm volatile(""
				 :
				 : "g"(Pointer)
				 : "memory");
#else
	VERSE_UNREACHABLE();
#endif
}

} // namespace Verse

#endif // WITH_VERSE_VM
