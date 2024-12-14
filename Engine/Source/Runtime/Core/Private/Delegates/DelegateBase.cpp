// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delegates/DelegateBase.h"

void* UE::Core::Private::DelegateAllocate(size_t Size, FDelegateAllocation& Allocation)
{
	int32 NewDelegateSize = FMath::DivideAndRoundUp((int32)Size, (int32)sizeof(FAlignedInlineDelegateType));
	if (Allocation.DelegateSize != NewDelegateSize)
	{
		Allocation.DelegateAllocator.ResizeAllocation(0, NewDelegateSize, sizeof(FAlignedInlineDelegateType));
		Allocation.DelegateSize = NewDelegateSize;
	}

	return Allocation.DelegateAllocator.GetAllocation();
}
