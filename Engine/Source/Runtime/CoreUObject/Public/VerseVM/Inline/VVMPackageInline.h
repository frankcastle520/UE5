// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMWeakCellMapInline.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMTupleType.h"

namespace Verse
{

template <typename FunctorType> // FunctorType is (VTupleType*) -> void
void VPackage::ForEachUsedTupleType(FunctorType&& F)
{
	if (UsedTupleTypes)
	{
		UsedTupleTypes->ForEach([&](VCell* Key, VCell* Value) { F(&Key->StaticCast<VTupleType>()); });
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM
