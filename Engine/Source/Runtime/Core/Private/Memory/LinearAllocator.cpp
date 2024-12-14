// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/LinearAllocator.h"


CORE_API FPersistentLinearAllocatorExtends GPersistentLinearAllocatorExtends;

static struct FInitializer
{
	FInitializer()
	{
		GPersistentLinearAllocatorExtends.Address = (uint64)GetPersistentLinearAllocator().GetBasePointer();
		GPersistentLinearAllocatorExtends.Size = (uint64)GetPersistentLinearAllocator().GetReservedMemorySize();
	}
} Initializer;


#if UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR

#include "CoreGlobals.h"
#include "Misc/ScopeLock.h"
#include "HAL/LowLevelMemTracker.h"

FLinearAllocator::FLinearAllocator(SIZE_T ReserveMemorySize)
	: Reserved(ReserveMemorySize)
{
	if (FPlatformMemory::CanOverallocateVirtualMemory() && ReserveMemorySize)
	{
		VirtualMemory = VirtualMemory.AllocateVirtual(ReserveMemorySize);
		if (!VirtualMemory.GetVirtualPointer())
		{
			UE_LOG(LogMemory, Warning, TEXT("LinearVirtualMemoryAllocator failed to reserve %u MB and will default to FMemory::Malloc instead"), ReserveMemorySize / 1024 / 1024);
			Reserved = 0;
		}
	}
	else
	{
#if PLATFORM_IOS || PLATFORM_TVOS
		UE_LOG(LogMemory, Warning, TEXT("LinearVirtualMemoryAllocator requires com.apple.developer.kernel.extended-virtual-addressing entitlement to work"));
#else
		UE_LOG(LogMemory, Warning, TEXT("This platform does not allow to allocate more virtual memory than there is physical memory. LinearVirtualMemoryAllocator will default to FMemory::Malloc instead"));
#endif
		Reserved = 0;
	}

	GPersistentLinearAllocatorExtends.Address = (uint64)VirtualMemory.GetVirtualPointer();
	GPersistentLinearAllocatorExtends.Size = (uint64)Reserved;
}

void* FLinearAllocator::Allocate(SIZE_T Size, uint32 Alignment)
{
	Alignment = FMath::Max(Alignment, 8u);
	{
		void* Mem = nullptr;
		{
			FScopeLock AutoLock(&Lock);
			if (CanFit(Size, Alignment))
			{
				CurrentOffset = Align(CurrentOffset, Alignment);
				const SIZE_T NewOffset = CurrentOffset + Size;
				if (NewOffset > Committed)
				{
					LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
					const size_t CommitGranularity = UE_USE_VERYLARGEPAGEALLOCATOR ? 2ul * 1024 * 1024 : 65536ul;
					const SIZE_T ToCommit = Align(NewOffset - Committed, FMath::Max(VirtualMemory.GetCommitAlignment(), CommitGranularity));
				#if UE_USE_VERYLARGEPAGEALLOCATOR
					if (!VirtualMemory.Commit(Committed, ToCommit, false))
					{
						// do not try to use linear allocator anymore and fallback to FMemory::Malloc from now on
						Reserved = Committed;
						return FMemory::Malloc(Size, Alignment);
					}
				#else
					VirtualMemory.Commit(Committed, ToCommit);
				#endif
					Committed += ToCommit;
					LLM_IF_ENABLED(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, (uint8*)VirtualMemory.GetVirtualPointer() + CurrentOffset, ToCommit));
				}
				Mem = (uint8*)VirtualMemory.GetVirtualPointer() + CurrentOffset;
				CurrentOffset += Size;
			}
		}
		if (Mem)
		{
			LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, Mem, Size, ELLMTag::Untagged, ELLMAllocType::FMalloc));
			return Mem;
		}
	}

	ExceedsReservation.fetch_add(Size, std::memory_order_relaxed);
	return FMemory::Malloc(Size, Alignment);
}

void FLinearAllocator::PreAllocate(SIZE_T Size, uint32 Alignment)
{
	Alignment = FMath::Max(Alignment, 8u);

	FScopeLock AutoLock(&Lock);
	if (CanFit(Size, Alignment))
	{
		CurrentOffset = Align(CurrentOffset, Alignment);
		const SIZE_T NewOffset = CurrentOffset + Size;
		if (NewOffset > Committed)
		{
			const SIZE_T ToCommit = Align(NewOffset - Committed, VirtualMemory.GetCommitAlignment());
			VirtualMemory.Commit(Committed, ToCommit);
			Committed += ToCommit;
		}
	}
}

bool FLinearAllocator::TryDeallocate(void* Ptr, SIZE_T Size)
{
	if (ContainsPointer(Ptr))
	{
		FScopeLock AutoLock(&Lock);
		if ((uint8*)Ptr + Size == (uint8*)VirtualMemory.GetVirtualPointer() + CurrentOffset)
		{
			LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, Ptr, ELLMAllocType::FMalloc));
			CurrentOffset -= Size;
			return true;
		}

		return false;
	}

	FMemory::Free(Ptr);
	return true;
}

bool FLinearAllocator::CanFit(SIZE_T Size, uint32 Alignment) const
{
	return (Reserved - Align(CurrentOffset, Alignment)) >= Size;
}

FLinearAllocator& GetPersistentLinearAllocator()
{
	static FLinearAllocator GPersistentLinearAllocator(UE_PERSISTENT_ALLOCATOR_RESERVE_SIZE);
	return GPersistentLinearAllocator;
}

#endif //~UE_ENABLE_LINEAR_VIRTUAL_ALLOCATOR