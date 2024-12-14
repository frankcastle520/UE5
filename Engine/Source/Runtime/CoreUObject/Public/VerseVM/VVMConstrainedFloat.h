// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMFloat.h"
#include "VerseVM/VVMType.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{

// An float type with constraints.
struct VConstrainedFloat : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VConstrainedFloat* New(FAllocationContext Context, VFloat InMin, VFloat InMax)
	{
		return new (Context.AllocateFastCell(sizeof(VConstrainedFloat))) VConstrainedFloat(Context, InMin, InMax);
	}
	static bool Equals(const VType& Type, VFloat Min, VFloat Max)
	{
		if (Type.IsA<VConstrainedFloat>())
		{
			const VConstrainedFloat& Other = Type.StaticCast<VConstrainedFloat>();
			return Min == Other.GetMin() && Max == Other.GetMax();
		}
		return false;
	}

	const VFloat& GetMin() const
	{
		return Min;
	}

	const VFloat& GetMax() const
	{
		return Max;
	}

	bool SubsumesImpl(FAllocationContext Context, VValue);

private:
	explicit VConstrainedFloat(FAllocationContext& Context, VFloat InMin, VFloat InMax)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, Min(InMin)
		, Max(InMax)
	{
	}

	VFloat Min;
	VFloat Max;
};
} // namespace Verse

#endif