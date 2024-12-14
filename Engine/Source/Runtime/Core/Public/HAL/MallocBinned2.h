// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/Allocators/CachedOSPageAllocator.h"
#include "HAL/Allocators/CachedOSVeryLargePageAllocator.h"
#include "HAL/Allocators/PooledVirtualMemoryAllocator.h"
#include "HAL/CriticalSection.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/MallocBinnedCommon.h"
#include "HAL/UnrealMemory.h"
#include "Math/NumericLimits.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Fork.h"
#include "Templates/Atomic.h"

struct FGenericMemoryStats;

#define UE_MB2_MAX_CACHED_OS_FREES (64)
#if PLATFORM_64BITS
#	define UE_MB2_MAX_CACHED_OS_FREES_BYTE_LIMIT (64*1024*1024)
#else
#	define UE_MB2_MAX_CACHED_OS_FREES_BYTE_LIMIT (16*1024*1024)
#endif

#define UE_MB2_LARGE_ALLOC					65536		// Alignment of OS-allocated pointer - pool-allocated pointers will have a non-aligned pointer

#if AGGRESSIVE_MEMORY_SAVING
#	define UE_MB2_MAX_SMALL_POOL_SIZE		(13104)		// Maximum bin size in SmallBinSizes in cpp file
#	define UE_MB2_SMALL_POOL_COUNT			48
#else
#	define UE_MB2_MAX_SMALL_POOL_SIZE		(32768-16)	// Maximum bin size in SmallBinSizes in cpp file
#	define UE_MB2_SMALL_POOL_COUNT			51
#endif


// When book keeping is at the end of FFreeBlock, MallocBinned2 cannot tell if the allocation comes from a large allocation (higher than 64KB, also named as "OSAllocation") 
// or from VeryLargePageAllocator that fell back to FCachedOSPageAllocator. In both cases the allocation (large or small) might be aligned to 64KB.
// bookKeeping at the end needs to be disabled if we want VeryLargePageAllocator to properly fallback to regular TCachedOSPageAllocator if needed
#ifndef UE_MB2_BOOKKEEPING_AT_THE_END_OF_LARGEBLOCK
#	define UE_MB2_BOOKKEEPING_AT_THE_END_OF_LARGEBLOCK 0
#endif

// If we are emulating forking on a windows server or are a linux server, enable support for avoiding dirtying pages owned by the parent. 
#ifndef BINNED2_FORK_SUPPORT
#	define BINNED2_FORK_SUPPORT (UE_SERVER && (PLATFORM_UNIX || DEFAULT_SERVER_FAKE_FORKS))
#endif

#define UE_MB2_ALLOCATOR_STATS				UE_MBC_ALLOCATOR_STATS
#define UE_MB2_ALLOCATOR_STATS_VALIDATION	(UE_MB2_ALLOCATOR_STATS && 0)

#if UE_MB2_ALLOCATOR_STATS_VALIDATION
#	include "Misc/ScopeLock.h"

	extern int64 AllocatedSmallPoolMemoryValidation;
	extern FCriticalSection ValidationCriticalSection;
	extern int32 RecursionCounter;
#endif

extern int32 EnableLegacyCachedOSPageAllocatorFreeMemReporting;

// Canary value used in FFreeBlock
// A constant value unless we're compiled with fork support in which case there are two values identifying whether the page
// was allocated pre- or post-fork
enum class EBlockCanary : uint8
{
	Zero = 0x0, // Not clear why this is needed by FreeBundles
#if BINNED2_FORK_SUPPORT
	PreFork = 0xb7,
	PostFork = 0xca,
#else
	Value = 0xe3 
#endif
};


//
// Optimized virtual memory allocator.
//
class FMallocBinned2 : public TMallocBinnedCommon<FMallocBinned2, UE_MB2_SMALL_POOL_COUNT, UE_MB2_MAX_SMALL_POOL_SIZE>
{
	struct FFreeBlock;

public:
	struct FPoolInfo
	{
		enum class ECanary : uint16
		{
			Unassigned = 0x3941,
			FirstFreeBlockIsOSAllocSize = 0x17ea,
			FirstFreeBlockIsPtr = 0xf317
		};

		uint16      Taken;          // Number of allocated elements in this pool, when counts down to zero can free the entire pool	
		ECanary     Canary;         // See ECanary
		uint32      AllocSize;      // Number of bytes allocated
		FFreeBlock* FirstFreeBlock; // Pointer to first free memory in this pool or the OS Allocation Size in bytes if this allocation is not binned
		FPoolInfo*  Next;           // Pointer to next pool
		FPoolInfo** PtrToPrevNext;  // Pointer to whichever pointer points to this pool

		FPoolInfo();

		void CheckCanary(ECanary ShouldBe) const;
		void SetCanary(ECanary ShouldBe, bool bPreexisting, bool bGuaranteedToBeNew);

		bool HasFreeBin() const;
		void* AllocateBin();

		SIZE_T GetOSRequestedBytes() const;
		SIZE_T GetOsAllocatedBytes() const;
		void SetOSAllocationSizes(SIZE_T InRequestedBytes, UPTRINT InAllocatedBytes);

		void Link(FPoolInfo*& PrevNext);
		void Unlink();

	private:
		void ExhaustPoolIfNecessary();
	};

private:
	// Forward declares.
	struct Private;

	/** Information about a piece of free memory. */
	struct FFreeBlock
	{
		FORCEINLINE FFreeBlock(uint32 InPageSize, uint16 InBinSize, uint8 InPoolIndex, EBlockCanary InCanary)
			: BinSize(InBinSize)
			, PoolIndex(InPoolIndex)
			, CanaryAndForkState(InCanary)
			, NextFreeBlock(nullptr)
		{
			check(InPoolIndex < MAX_uint8 && InBinSize <= MAX_uint16);
			NumFreeBins = InPageSize / InBinSize;
			if (NumFreeBins * InBinSize + sizeof(FFreeBlock) > InPageSize)
			{
				NumFreeBins--;
			}
			check(NumFreeBins * InBinSize + sizeof(FFreeBlock) <= InPageSize);
		}

		FORCEINLINE uint32 GetNumFreeBins() const
		{
			return NumFreeBins;
		}

		FORCEINLINE void* AllocateBin()
		{
			--NumFreeBins;
#if !UE_USE_VERYLARGEPAGEALLOCATOR || !UE_MB2_BOOKKEEPING_AT_THE_END_OF_LARGEBLOCK
			if (IsAligned(this, UE_MB2_LARGE_ALLOC))
			{
				return (uint8*)this + UE_MB2_LARGE_ALLOC - (NumFreeBins + 1) * BinSize;
			}
#else
			if (IsAligned((uintptr_t)this + sizeof(FFreeBlock), UE_MB2_LARGE_ALLOC))
			{
				// The book keeping FreeBlock is at the end of the "page" so we align down to get to the beginning of the page
				const uintptr_t Ptr = AlignDown((uintptr_t)this, UE_MB2_LARGE_ALLOC);
				// And we offset the returned pointer based on how many free blocks are left.
				return (uint8*)Ptr + (NumFreeBins * BinSize);
			}
#endif
			return (uint8*)this + (NumFreeBins * BinSize);
		}

		uint16 BinSize;				// Size of the bins that this list points to
		uint8 PoolIndex;			// Index of this pool

		// Normally this value just functions as a canary to detect invalid memory state.
		// When process forking is supported, it's still a canary but it has two valid values.
		// One value is used pre-fork and one post-fork and the value is used to avoid freeing memory in pages shared with the parent process.
		EBlockCanary CanaryAndForkState; 

		uint32 NumFreeBins;          // Number of consecutive free bins here, at least 1.
		FFreeBlock*  NextFreeBlock;  // Next free block in another pool
	};

	struct FPoolList
	{
		FPoolList() = default;

		void Clear();
		bool IsEmpty() const;

		      FPoolInfo& GetFrontPool();
		const FPoolInfo& GetFrontPool() const;

		void LinkToFront(FPoolInfo* Pool);

		FPoolInfo& PushNewPoolToFront(FMallocBinned2& Allocator, uint32 InBytes, uint32 InPoolIndex);

		void ValidateActivePools() const;
		void ValidateExhaustedPools() const;

	private:
		FPoolInfo* Front = nullptr;
	};

	/** Pool table. */
	struct FPoolTable
	{
		FPoolList ActivePools;
		FPoolList ExhaustedPools;
		uint32    BinSize = 0;

		FCriticalSection Mutex;

		FPoolTable() = default;
	};

	// Pool tables for different pool sizes
	FPoolTable SmallPoolTables[UE_MB2_SMALL_POOL_COUNT];

#if BINNED2_FORK_SUPPORT
	EBlockCanary CurrentCanary = EBlockCanary::PreFork; // The value of the canary for pages we have allocated this side of the fork 
	EBlockCanary OldCanary = EBlockCanary::PreFork;		// If we have forked, the value canary of old pages we should avoid touching 
#else 
	static constexpr EBlockCanary CurrentCanary = EBlockCanary::Value;
#endif

#if !PLATFORM_UNIX && !PLATFORM_ANDROID
#	if UE_USE_VERYLARGEPAGEALLOCATOR
		FCachedOSVeryLargePageAllocator CachedOSPageAllocator;
#	else
		TCachedOSPageAllocator<UE_MB2_MAX_CACHED_OS_FREES, UE_MB2_MAX_CACHED_OS_FREES_BYTE_LIMIT> CachedOSPageAllocator;
#	endif
#else
	FPooledVirtualMemoryAllocator CachedOSPageAllocator;
#endif

	FORCEINLINE bool IsOSAllocation(const void* Ptr) const
	{
#if UE_USE_VERYLARGEPAGEALLOCATOR && !PLATFORM_UNIX && !PLATFORM_ANDROID
		return !CachedOSPageAllocator.IsSmallBlockAllocation(Ptr) && IsAligned(Ptr, UE_MB2_LARGE_ALLOC);
#else
		return IsAligned(Ptr, UE_MB2_LARGE_ALLOC);
#endif
	}

	static FORCEINLINE FFreeBlock* GetPoolHeaderFromPointer(void* Ptr)
	{
#if !UE_USE_VERYLARGEPAGEALLOCATOR || !UE_MB2_BOOKKEEPING_AT_THE_END_OF_LARGEBLOCK
		return (FFreeBlock*)AlignDown(Ptr, UE_MB2_LARGE_ALLOC);
#else
		return (FFreeBlock*)(AlignDown((uintptr_t) Ptr, UE_MB2_LARGE_ALLOC) + UE_MB2_LARGE_ALLOC - sizeof(FFreeBlock));
#endif
	}

public:
	CORE_API FMallocBinned2();
	CORE_API virtual ~FMallocBinned2();

	// FMalloc interface.
	CORE_API virtual bool IsInternallyThreadSafe() const override;

	FORCEINLINE virtual void* Malloc(SIZE_T Size, uint32 Alignment) override
	{
#if UE_MB2_ALLOCATOR_STATS_VALIDATION
		FScopeLock Lock(&ValidationCriticalSection);
		++RecursionCounter;
		void *Result = MallocInline(Size, Alignment);
		if (!IsOSAllocation(Result))
		{
			SIZE_T OutSize;
			ensure(GetAllocationSize(Result, OutSize));
			AllocatedSmallPoolMemoryValidation += OutSize;
			if (RecursionCounter == 1)
			{
				check(GetTotalAllocatedSmallPoolMemory() == AllocatedSmallPoolMemoryValidation);
				if (GetTotalAllocatedSmallPoolMemory() != AllocatedSmallPoolMemoryValidation)
				{
					UE_DEBUG_BREAK();
				}
			}
		}
		--RecursionCounter;
		return Result;
#else
		return MallocInline(Size, Alignment);
#endif
	}

	FORCEINLINE void* MallocInline(SIZE_T Size, uint32 Alignment)
	{
		// Only allocate from the small pools if the size is small enough and the alignment isn't crazy large.
		// With large alignments, we'll waste a lot of memory allocating an entire page, but such alignments are highly unlikely in practice.
		const bool bUseSmallPool = UseSmallAlloc(Size, Alignment);
		if (bUseSmallPool)
		{
			FPerThreadFreeBlockLists* Lists = GMallocBinnedPerThreadCaches ? FPerThreadFreeBlockLists::Get() : nullptr;
			if (Lists)
			{
				const uint32 PoolIndex = BoundSizeToPoolIndex(Size, MemSizeToPoolIndex);
				if (void* Result = Lists->Malloc(PoolIndex))
				{
#if UE_MB2_ALLOCATOR_STATS
					const uint32 BinSize = PoolIndexToBinSize(PoolIndex);
					Lists->AllocatedMemory += BinSize;
#endif
					return Result;
				}
			}
		}

		return MallocSelect(Size, Alignment, bUseSmallPool);
	}

	FORCEINLINE static bool UseSmallAlloc(SIZE_T Size, uint32 Alignment)
	{
#if UE_USE_VERYLARGEPAGEALLOCATOR && UE_MB2_BOOKKEEPING_AT_THE_END_OF_LARGEBLOCK
		if (Alignment > UE_MBC_MIN_SMALL_POOL_ALIGNMENT)
		{
			Size = Align(Size, Alignment);
		}
		const bool bResult = (Size <= UE_MB2_MAX_SMALL_POOL_SIZE);
#else
		const bool bResult = ((Size <= UE_MB2_MAX_SMALL_POOL_SIZE) & (Alignment <= UE_MBC_MIN_SMALL_POOL_ALIGNMENT)); // one branch, not two
#endif
		return bResult;
	}

	CORE_API void* MallocSelect(SIZE_T Size, uint32 Alignment, bool bUseSmallPool);
	FORCEINLINE void* MallocSelect(SIZE_T Size, uint32 Alignment)
	{
		return MallocSelect(Size, Alignment, UseSmallAlloc(Size, Alignment));
	}

	FORCEINLINE virtual void* Realloc(void* Ptr, SIZE_T NewSize, uint32 Alignment) override
	{
#if UE_MB2_ALLOCATOR_STATS_VALIDATION
		const bool bOldIsOsAllocation = IsOSAllocation(Ptr);
		SIZE_T OldSize;
		if (!bOldIsOsAllocation)
		{
			ensure(GetAllocationSize(Ptr, OldSize));
		}

		FScopeLock Lock(&ValidationCriticalSection);
		++RecursionCounter;
		void *Result = ReallocInline(Ptr, NewSize, Alignment);
		if (!bOldIsOsAllocation)
		{
			AllocatedSmallPoolMemoryValidation -= OldSize;
		}
		if (!IsOSAllocation(Result))
		{
			SIZE_T OutSize;
			ensure(GetAllocationSize(Result, OutSize));
			AllocatedSmallPoolMemoryValidation += OutSize;
		}
		if (RecursionCounter == 1)
		{
			check(GetTotalAllocatedSmallPoolMemory() == AllocatedSmallPoolMemoryValidation);
			if (GetTotalAllocatedSmallPoolMemory() != AllocatedSmallPoolMemoryValidation)
			{
				UE_DEBUG_BREAK();
			}
		}
		--RecursionCounter;
		return Result;
#else
		return ReallocInline(Ptr, NewSize, Alignment);
#endif
	}

	FORCEINLINE void* ReallocInline(void* Ptr, SIZE_T NewSize, uint32 Alignment) 
	{
#if UE_USE_VERYLARGEPAGEALLOCATOR && UE_MB2_BOOKKEEPING_AT_THE_END_OF_LARGEBLOCK
		if (Alignment > UE_MBC_MIN_SMALL_POOL_ALIGNMENT && (NewSize <= UE_MB2_MAX_SMALL_POOL_SIZE))
		{
			NewSize = Align(NewSize, Alignment);
		}
		if (NewSize <= UE_MB2_MAX_SMALL_POOL_SIZE)
#else
		if (NewSize <= UE_MB2_MAX_SMALL_POOL_SIZE && Alignment <= UE_MBC_MIN_SMALL_POOL_ALIGNMENT) // one branch, not two
#endif
		{
			FPerThreadFreeBlockLists* Lists = GMallocBinnedPerThreadCaches ? FPerThreadFreeBlockLists::Get() : nullptr;
			if (Lists && (!Ptr || !IsOSAllocation(Ptr)))
			{
				uint32 BinSize = 0;
				uint32 PoolIndex = 0;

				bool bCanFree = true; // the nullptr is always "freeable"
				if (Ptr)
				{
					// Reallocate to a smaller/bigger pool if necessary
					FFreeBlock* Free = GetPoolHeaderFromPointer(Ptr);
					BinSize = Free->BinSize;
					PoolIndex = Free->PoolIndex;
					// If canary is invalid we will assert in ReallocExternal. Otherwise it's the pre-fork canary and we will allocate new memory without touching this allocation.
					bCanFree = Free->CanaryAndForkState == CurrentCanary;
					if (NewSize && bCanFree && NewSize <= BinSize && (PoolIndex == 0 || NewSize > PoolIndexToBinSize(PoolIndex - 1)))
					{
						return Ptr;
					}
					bCanFree = bCanFree && Lists->CanFree(PoolIndex, BinSize);
				}
				if (bCanFree)
				{
					const uint32 NewPoolIndex = BoundSizeToPoolIndex(NewSize, MemSizeToPoolIndex);
					const uint32 NewBinSize = PoolIndexToBinSize(NewPoolIndex);
					void* Result = NewSize ? Lists->Malloc(NewPoolIndex) : nullptr;
#if UE_MB2_ALLOCATOR_STATS
					if (Result)
					{
						Lists->AllocatedMemory += NewBinSize;
					}
#endif
					if (Result || !NewSize)
					{
						if (Result && Ptr)
						{
							FMemory::Memcpy(Result, Ptr, FPlatformMath::Min<SIZE_T>(NewSize, BinSize));
						}
						if (Ptr)
						{
							const bool bDidPush = Lists->Free(Ptr, PoolIndex, BinSize);
							checkSlow(bDidPush);
#if UE_MB2_ALLOCATOR_STATS
							Lists->AllocatedMemory -= BinSize;
#endif
						}

						return Result;
					}
				}
			}
		}
		void* Result = ReallocExternal(Ptr, NewSize, Alignment);
		return Result;
	}

	FORCEINLINE virtual void Free(void* Ptr) override
	{
#if UE_MB2_ALLOCATOR_STATS_VALIDATION
		FScopeLock Lock(&ValidationCriticalSection);
		++RecursionCounter;
		if (!IsOSAllocation(Ptr))
		{
			SIZE_T OutSize;
			ensure(GetAllocationSize(Ptr, OutSize));
			AllocatedSmallPoolMemoryValidation -= OutSize;
		}
		FreeInline(Ptr);
		if (RecursionCounter == 1)
		{
			check(GetTotalAllocatedSmallPoolMemory() == AllocatedSmallPoolMemoryValidation);
			if (GetTotalAllocatedSmallPoolMemory() != AllocatedSmallPoolMemoryValidation)
			{
				UE_DEBUG_BREAK();
			}
		}
		--RecursionCounter;
#else
		FreeInline(Ptr);
#endif
	}

	FORCEINLINE void FreeInline(void* Ptr)
	{
		if (!IsOSAllocation(Ptr))
		{
			FPerThreadFreeBlockLists* Lists = GMallocBinnedPerThreadCaches ? FPerThreadFreeBlockLists::Get() : nullptr;
			if (Lists)
			{
				FFreeBlock* BasePtr = GetPoolHeaderFromPointer(Ptr);
				const int32 BinSize = BasePtr->BinSize;
				// If canary is invalid we will assert in FreeExternal. Otherwise it's the pre-fork canary and we will turn this free into a no-op.
				if (BasePtr->CanaryAndForkState == CurrentCanary && Lists->Free(Ptr, BasePtr->PoolIndex, BinSize))
				{
#if UE_MB2_ALLOCATOR_STATS
					Lists->AllocatedMemory -= BinSize;
#endif
					return;
				}
			}
		}
		FreeExternal(Ptr);
	}

	FORCEINLINE bool GetSmallAllocationSize(void* Ptr, SIZE_T& SizeOut) const
	{
		if (!IsOSAllocation(Ptr))
		{
			const FFreeBlock* Free = GetPoolHeaderFromPointer(Ptr);
			CanaryTest(Free);
			SizeOut = Free->BinSize;
			return true;
		}
		return false;
	}

	FORCEINLINE virtual bool GetAllocationSize(void *Ptr, SIZE_T &SizeOut) override
	{
		if (GetSmallAllocationSize(Ptr, SizeOut))
		{
			return true;
		}
		return GetAllocationSizeExternal(Ptr, SizeOut);
	}

	FORCEINLINE virtual SIZE_T QuantizeSize(SIZE_T Count, uint32 Alignment) override
	{
		return QuantizeSizeCommon(Count, Alignment, *this);
	}

	CORE_API virtual bool ValidateHeap() override;
	CORE_API virtual void Trim(bool bTrimThreadCaches) override;
	CORE_API virtual const TCHAR* GetDescriptiveName() override;
	CORE_API virtual void UpdateStats() override;
	CORE_API virtual void OnMallocInitialized() override;
	CORE_API virtual void OnPreFork() override;
	CORE_API virtual void OnPostFork() override;
	virtual uint64 GetFreeCachedMemorySize() const
	{
		if (EnableLegacyCachedOSPageAllocatorFreeMemReporting)
		{
			return CachedOSPageAllocator.GetCachedFreeTotal();
		}

		return CachedOSPageAllocator.GetCachedImmediatelyFreeable();
	}
	// End FMalloc interface.

	CORE_API void* MallocExternalSmall(SIZE_T Size, uint32 Alignment);
	CORE_API void* MallocExternalLarge(SIZE_T Size, uint32 Alignment);
	CORE_API void* ReallocExternal(void* Ptr, SIZE_T NewSize, uint32 Alignment);
	CORE_API void FreeExternal(void *Ptr);

	CORE_API void CanaryFail(const FFreeBlock* Block) const;
	FORCEINLINE void CanaryTest(const FFreeBlock* Block) const
	{
#if BINNED2_FORK_SUPPORT
		// When we support forking there are two valid canary values.
		if (Block->CanaryAndForkState != CurrentCanary && Block->CanaryAndForkState != OldCanary)
#else
		if (Block->CanaryAndForkState != CurrentCanary)
#endif
		{
			CanaryFail(Block);
		}
	}

	CORE_API virtual void GetAllocatorStats( FGenericMemoryStats& out_Stats ) override;
	/** Dumps current allocator stats to the log. */
	CORE_API virtual void DumpAllocatorStats(class FOutputDevice& Ar) override;
	
	static CORE_API uint16 SmallBinSizesReversed[UE_MB2_SMALL_POOL_COUNT]; // this is reversed to get the smallest elements on our main cache line
	static CORE_API FMallocBinned2* MallocBinned2;
	static CORE_API uint32 PageSize;
	// Mapping of sizes to small table indices
	static CORE_API uint8 MemSizeToPoolIndex[1 + (UE_MB2_MAX_SMALL_POOL_SIZE >> UE_MBC_MIN_SMALL_POOL_ALIGNMENT_SHIFT)];

	static void* AllocateMetaDataMemory(SIZE_T Size);
	static void FreeMetaDataMemory(void* Ptr, SIZE_T Size);

	FORCEINLINE uint32 PoolIndexToBinSize(uint32 PoolIndex) const
	{
		return SmallBinSizesReversed[UE_MB2_SMALL_POOL_COUNT - PoolIndex - 1];
	}

	void FreeBundles(FBundleNode* Bundles, uint32 PoolIndex);

	void FlushCurrentThreadCacheInternal(bool bNewEpochOnly = false);
};

#define UE_MB2_INLINE (1)
#if UE_MB2_INLINE // during development, it helps with iteration time to not include these here, but rather in the .cpp
	#if PLATFORM_USES_FIXED_GMalloc_CLASS && !FORCE_ANSI_ALLOCATOR && USE_MALLOC_BINNED2
		#define FMEMORY_INLINE_FUNCTION_DECORATOR  FORCEINLINE
		#define FMEMORY_INLINE_GMalloc (FMallocBinned2::MallocBinned2)
		#include "FMemory.inl" // IWYU pragma: export
	#endif
#endif

