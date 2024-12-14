// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMType.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{

// An int type with constraints. A uninitialized min/max means no constraint.
struct VConstrainedInt : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VConstrainedInt* New(FAllocationContext Context, VInt InMin, VInt InMax)
	{
		return new (Context.AllocateFastCell(sizeof(VConstrainedInt))) VConstrainedInt(Context, InMin, InMax);
	}

	const VInt GetMin() const
	{
		return Min.Get();
	}

	const VInt GetMax() const
	{
		return Max.Get();
	}

	bool SubsumesImpl(FAllocationContext Context, VValue);

private:
	explicit VConstrainedInt(FAllocationContext& Context, VInt InMin, VInt InMax)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, Min(Context, InMin)
		, Max(Context, InMax)
	{
	}

	TWriteBarrier<VInt> Min;
	TWriteBarrier<VInt> Max;
};

} // namespace Verse
#endif // WITH_VERSE_VM
