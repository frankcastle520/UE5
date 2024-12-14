// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "HAL/UnrealMemory.h"

#ifndef UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR
	#define UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR PLATFORM_HAS_FPlatformVirtualMemoryBlock
#endif

#if UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR
#include <atomic>
#include "HAL/CriticalSection.h"

struct FLinearAllocator
{
	CORE_API FLinearAllocator(SIZE_T ReserveMemorySize);

	FORCEINLINE ~FLinearAllocator()
	{
		VirtualMemory.FreeVirtual();
	}

	CORE_API void* Allocate(SIZE_T Size, uint32 Alignment = 8);
	CORE_API void  PreAllocate(SIZE_T Size, uint32 Alignment = 8);

	// This will succeed only when deallocating the last allocation
	CORE_API bool  TryDeallocate(void* Ptr, SIZE_T Size);

	FORCEINLINE bool ContainsPointer(const void* Ptr) const
	{
		return (uintptr_t)Ptr - (uintptr_t)VirtualMemory.GetVirtualPointer() < Reserved;
	}

	FORCEINLINE SIZE_T GetExceedingSize() const
	{
		return ExceedsReservation.load(std::memory_order_relaxed);
	}

	FORCEINLINE bool IsInitialized() const
	{
		return Reserved != 0;
	}

	FORCEINLINE SIZE_T GetAllocatedMemorySize() const
	{
		return Committed;
	}

	FORCEINLINE SIZE_T GetReservedMemorySize() const
	{
		return Reserved;
	}

	FORCEINLINE const void* GetBasePointer() const
	{
		return VirtualMemory.GetVirtualPointer();
	}

private:
	FCriticalSection Lock;
	FPlatformMemory::FPlatformVirtualMemoryBlock VirtualMemory;
	SIZE_T Reserved;
	SIZE_T Committed = 0;
	SIZE_T CurrentOffset = 0;
	std::atomic<SIZE_T> ExceedsReservation = 0;

	bool CanFit(SIZE_T Size, uint32 Alignment) const;
};

CORE_API FLinearAllocator& GetPersistentLinearAllocator();

#else
// stub implementation with most functions being nop
struct FLinearAllocator
{
	FORCEINLINE FLinearAllocator(SIZE_T) {}

	FORCEINLINE void*	Allocate(SIZE_T Size, uint32 Alignment = 8) { return FMemory::Malloc(Size, Alignment); }
	FORCEINLINE void	PreAllocate(SIZE_T, uint32) {}
	FORCEINLINE bool	TryDeallocate(void* Ptr, SIZE_T Size)	{ FMemory::Free(Ptr); return true; }

	FORCEINLINE bool	ContainsPointer(const void*) const	{ return false; }
	FORCEINLINE SIZE_T	GetExceedingSize() const			{ return 0; }
	FORCEINLINE bool	IsInitialized() const				{ return false; }
	FORCEINLINE SIZE_T	GetAllocatedMemorySize() const		{ return 0; }
	FORCEINLINE SIZE_T	GetReservedMemorySize() const		{ return 0; }
	FORCEINLINE const void* GetBasePointer() const			{ return nullptr; }
};

FORCEINLINE FLinearAllocator GetPersistentLinearAllocator() { return FLinearAllocator(0); }

#endif	//~UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR

struct FPersistentLinearAllocatorExtends
{
	uint64 Address;
	uint64 Size;
};

// Special case for the FPermanentObjectPoolExtents to reduce the amount of pointer dereferencing
extern CORE_API FPersistentLinearAllocatorExtends GPersistentLinearAllocatorExtends;