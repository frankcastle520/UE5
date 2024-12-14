// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnObjAllocator.h: Unreal object allocation
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformMath.h"
#include "Memory/LinearAllocator.h"

class UObjectBase;

class FUObjectAllocator
{
public:
	/**
	 * Allocates and initializes the permanent object pool
	 *
	 * @param InPermanentObjectPoolSize size of permanent object pool
	 */
	UE_DEPRECATED(5.5, "Permanent Object Pool is handled by the global instance of FLinearAllocator now")
	COREUOBJECT_API void AllocatePermanentObjectPool(int32 InPermanentObjectPoolSize) {}

	/**
	 * Prints a debugf message to allow tuning
	 */
	COREUOBJECT_API void BootMessage();

	/**
	 * Disables allocation of objects from the persistend allocator
	 * Needed by the Editor to be able to clean up all objects
	 */
	COREUOBJECT_API void DisablePersistentAllocator();

	/**
	 * Allocates a UObjectBase from the free store or the permanent object pool
	 *
	 * @param Size size of uobject to allocate
	 * @param Alignment alignment of uobject to allocate
	 * @param bAllowPermanent if true, allow allocation in the permanent object pool, if it fits
	 * @return newly allocated UObjectBase (not really a UObjectBase yet, no constructor like thing has been called).
	 */
	COREUOBJECT_API UObjectBase* AllocateUObject(int32 Size, int32 Alignment, bool bAllowPermanent);

	/**
	 * Returns a UObjectBase to the free store, unless it is in the permanent object pool
	 *
	 * @param Object object to free
	 */
	COREUOBJECT_API void FreeUObject(UObjectBase *Object) const;
};

/** Global UObjectBase allocator							*/
extern COREUOBJECT_API FUObjectAllocator GUObjectAllocator;

/** Helps check if an object is part of permanent object pool */
class FPermanentObjectPoolExtents
{
public:
	FORCEINLINE FPermanentObjectPoolExtents(const FPersistentLinearAllocatorExtends& InAllocatorExtends = GPersistentLinearAllocatorExtends)
		: Address(InAllocatorExtends.Address)
		, Size(InAllocatorExtends.Size)
	{}

	FORCEINLINE bool Contains(const UObjectBase* Object) const
	{
		return reinterpret_cast<uint64>(Object) - Address < Size;
	}

private:
	const uint64 Address;
	const uint64 Size;
};
