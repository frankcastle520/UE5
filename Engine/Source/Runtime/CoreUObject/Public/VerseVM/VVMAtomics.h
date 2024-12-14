// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "HAL/Platform.h"
#include <atomic>

namespace Verse
{

//// Just a compiler fence. Has no effect on the hardware, but tells the compiler
//// not to move things around this call. Should not affect the compiler's ability
//// to do things like register allocation and code motion over pure operations.
inline void CompilerFence()
{
#if PLATFORM_WINDOWS
	_ReadWriteBarrier();
#else
	asm volatile("" ::
					 : "memory");
#endif
}

#if PLATFORM_CPU_ARM_FAMILY

//// Full memory fence. No accesses will float above this, and no accesses will sink below it.
inline void ArmDmb()
{
	asm volatile("dmb ish" ::
					 : "memory");
}

// Like the above, but only affects stores.
inline void ArmDmbSt()
{
	asm volatile("dmb ishst" ::
					 : "memory");
}

inline void ArmIsb()
{
	asm volatile("isb" ::
					 : "memory");
}

inline void LoadLoadFence()
{
	ArmDmb();
}
inline void LoadStoreFence()
{
	ArmDmb();
}
inline void StoreLoadFence()
{
	ArmDmb();
}
inline void StoreStoreFence()
{
	ArmDmbSt();
}
inline void CrossModifyingCodeFence()
{
	ArmIsb();
}

#elif PLATFORM_CPU_X86_FAMILY

inline void X86Ortop()
{
#if PLATFORM_WINDOWS
	FGenericPlatformMisc::MemoryBarrier();
#elif PLATFORM_64BITS
	asm volatile("lock; orl $0, (%%rsp)" ::
					 : "memory");
#else
	asm volatile("lock; orl $0, (%%esp)" ::
					 : "memory");
#endif
}

inline void X86Cpuid()
{
#if PLATFORM_WINDOWS
	int info[4];
	__cpuid(info, 0);
#else
	intptr_t a = 0, b, c, d;
	asm volatile(
		"cpuid"
		: "+a"(a), "=b"(b), "=c"(c), "=d"(d)
		:
		: "memory");
#endif
}

inline void LoadLoadFence()
{
	CompilerFence();
}
inline void LoadStoreFence()
{
	CompilerFence();
}
inline void StoreLoadFence()
{
	X86Ortop();
}
inline void StoreStoreFence()
{
	CompilerFence();
}
inline void CrossModifyingCodeFence()
{
	X86Cpuid();
}

#else

inline void LoadLoadFence()
{
	std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline void LoadStoreFence()
{
	std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline void StoreLoadFence()
{
	std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline void StoreStoreFence()
{
	std::atomic_thread_fence(std::memory_order_seq_cst);
}
inline void CrossModifyingCodeFence()
{
	std::atomic_thread_fence(std::memory_order_seq_cst);
} // Probably not strong enough.

#endif

} // namespace Verse
#endif // WITH_VERSE_VM
