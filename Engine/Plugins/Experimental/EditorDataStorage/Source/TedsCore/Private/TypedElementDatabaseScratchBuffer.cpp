// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseScratchBuffer.h"

namespace UE::Editor::DataStorage
{
	FScratchBuffer::FScratchBuffer()
		: CurrentAllocator(&Allocators[0])
		, PreviousAllocator(&Allocators[1])
		, LeastRecentAllocator(&Allocators[2])
	{
	}

	void* FScratchBuffer::Allocate(size_t Size, size_t Alignment)
	{
		return CurrentAllocator.load()->Malloc(Size, Alignment);
	}

	void FScratchBuffer::BatchDelete()
	{
		MemoryAllocator* Current = CurrentAllocator.exchange(LeastRecentAllocator);
		LeastRecentAllocator = PreviousAllocator;
		PreviousAllocator = Current;
		LeastRecentAllocator->BulkDelete();
	}
} // namespace UE::Editor::DataStorage
