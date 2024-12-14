// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"
#include "VVMContext.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMInt.h"
#include "VVMValue.h"

namespace Verse
{

struct VRational : VHeapValue
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VHeapValue);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	TWriteBarrier<VInt> Numerator;
	TWriteBarrier<VInt> Denominator;

	static VRational& Add(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static VRational& Sub(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static VRational& Mul(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static VRational& Div(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static VRational& Neg(FAllocationContext Context, VRational& N);
	static bool Eq(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static bool Gt(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static bool Lt(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static bool Gte(FAllocationContext Context, VRational& Lhs, VRational& Rhs);
	static bool Lte(FAllocationContext Context, VRational& Lhs, VRational& Rhs);

	VInt Floor(FAllocationContext Context) const;
	VInt Ceil(FAllocationContext Context) const;

	void Reduce(FAllocationContext Context);
	void NormalizeSigns(FAllocationContext Context);
	bool IsZero() const { return Numerator.Get().IsZero(); }
	bool IsReduced() const { return bIsReduced; }

	static VRational& New(FAllocationContext Context, VInt InNumerator, VInt InDenominator)
	{
		return *new (Context.AllocateFastCell(sizeof(VRational))) VRational(Context, InNumerator, InDenominator);
	}

	COREUOBJECT_API bool EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder);

	COREUOBJECT_API uint32 GetTypeHashImpl();

	COREUOBJECT_API void ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter);

	static void SerializeImpl(VRational*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

private:
	VRational(FAllocationContext Context, VInt InNumerator, VInt InDenominator)
		: VHeapValue(Context, &GlobalTrivialEmergentType.Get(Context))
		, bIsReduced(false)
	{
		checkSlow(!InDenominator.IsZero());
		Numerator.Set(Context, InNumerator);
		Denominator.Set(Context, InDenominator);
	}

	bool bIsReduced;
};

} // namespace Verse
#endif // WITH_VERSE_VM
