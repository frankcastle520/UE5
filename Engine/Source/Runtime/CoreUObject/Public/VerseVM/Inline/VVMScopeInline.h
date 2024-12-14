// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMScope.h"

namespace Verse
{
inline VScope& VScope::New(FAllocationContext Context, VClass* InSuperClass)
{
	const size_t NumBytes = sizeof(VScope);
	return *new (Context.AllocateFastCell(NumBytes)) VScope(Context, InSuperClass);
}

inline VScope::VScope(FAllocationContext Context, VClass* InSuperClass)
	: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
	, SuperClass(Context, InSuperClass)
{
}

} // namespace Verse
#endif // WITH_VERSE_VM
