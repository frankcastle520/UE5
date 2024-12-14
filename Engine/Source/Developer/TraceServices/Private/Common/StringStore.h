// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Common/SlabAllocator.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "HAL/CriticalSection.h"

namespace TraceServices
{

class FStringStore
{
public:
	FStringStore(FSlabAllocator& Allocator);
	const TCHAR* Store(const TCHAR* String);
	const TCHAR* Store(const FStringView& String);

private:
	enum
	{
		BlockSize = 4 << 20
	};
	FCriticalSection Cs;
	FSlabAllocator& Allocator;
	TMultiMap<uint32, const TCHAR*> StoredStrings;
	TArray<const TCHAR*> FindStoredStrings;
	TCHAR* BufferPtr = nullptr;
	uint64 BufferLeft = 0;
	uint64 BlockCount = 0;
};

} // namespace TraceServices
