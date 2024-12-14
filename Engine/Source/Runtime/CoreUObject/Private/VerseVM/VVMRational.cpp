// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMRational.h"
#include "Templates/TypeHash.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VRational);
TGlobalTrivialEmergentTypePtr<&VRational::StaticCppClassInfo> VRational::GlobalTrivialEmergentType;

VRational& VRational::Add(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	if (VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()))
	{
		return VRational::New(Context,
			VInt::Add(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
			Lhs.Denominator.Get());
	}

	return VRational::New(
		Context,
		VInt::Add(Context,
			VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
			VInt::Mul(Context, Rhs.Numerator.Get(), Lhs.Denominator.Get())),
		VInt::Mul(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()));
}

VRational& VRational::Sub(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	if (VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()))
	{
		return VRational::New(Context,
			VInt::Sub(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
			Lhs.Denominator.Get());
	}

	return VRational::New(
		Context,
		VInt::Sub(Context,
			VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
			VInt::Mul(Context, Rhs.Numerator.Get(), Lhs.Denominator.Get())),
		VInt::Mul(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()));
}

VRational& VRational::Mul(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	return VRational::New(Context,
		VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Numerator.Get()),
		VInt::Mul(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()));
}

VRational& VRational::Div(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	return VRational::New(Context,
		VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
		VInt::Mul(Context, Lhs.Denominator.Get(), Rhs.Numerator.Get()));
}

VRational& VRational::Neg(FAllocationContext Context, VRational& N)
{
	return VRational::New(Context, VInt::Neg(Context, N.Numerator.Get()), N.Denominator.Get());
}

bool VRational::Eq(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	Lhs.Reduce(Context);
	Lhs.NormalizeSigns(Context);
	Rhs.Reduce(Context);
	Rhs.NormalizeSigns(Context);

	return VInt::Eq(Context, Lhs.Numerator.Get(), Rhs.Numerator.Get())
		&& VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get());
}

bool VRational::Gt(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	if (VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()))
	{
		return VInt::Gt(Context, Lhs.Numerator.Get(), Rhs.Numerator.Get());
	}

	return VInt::Gt(Context,
		VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
		VInt::Mul(Context, Rhs.Numerator.Get(), Lhs.Denominator.Get()));
}

bool VRational::Lt(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	if (VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()))
	{
		return VInt::Lt(Context, Lhs.Numerator.Get(), Rhs.Numerator.Get());
	}

	return VInt::Lt(Context,
		VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
		VInt::Mul(Context, Rhs.Numerator.Get(), Lhs.Denominator.Get()));
}

bool VRational::Gte(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	if (VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()))
	{
		return VInt::Gte(Context, Lhs.Numerator.Get(), Rhs.Numerator.Get());
	}

	return VInt::Gte(Context,
		VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
		VInt::Mul(Context, Rhs.Numerator.Get(), Lhs.Denominator.Get()));
}

bool VRational::Lte(FAllocationContext Context, VRational& Lhs, VRational& Rhs)
{
	if (VInt::Eq(Context, Lhs.Denominator.Get(), Rhs.Denominator.Get()))
	{
		return VInt::Lte(Context, Lhs.Numerator.Get(), Rhs.Numerator.Get());
	}

	return VInt::Lte(Context,
		VInt::Mul(Context, Lhs.Numerator.Get(), Rhs.Denominator.Get()),
		VInt::Mul(Context, Rhs.Numerator.Get(), Lhs.Denominator.Get()));
}

VInt VRational::Floor(FAllocationContext Context) const
{
	VInt IntNumerator(Numerator.Get());
	VInt IntDenominator(Denominator.Get());
	bool bHasNonZeroRemainder = false;
	VInt IntQuotient = VInt::Div(Context, IntNumerator, IntDenominator, &bHasNonZeroRemainder);
	if (bHasNonZeroRemainder && (IntNumerator.IsNegative() != IntDenominator.IsNegative()))
	{
		IntQuotient = VInt::Sub(Context, IntQuotient, VInt(1));
	}
	return IntQuotient;
}

VInt VRational::Ceil(FAllocationContext Context) const
{
	VInt IntNumerator(Numerator.Get());
	VInt IntDenominator(Denominator.Get());
	bool bHasNonZeroRemainder = false;
	VInt IntQuotient = VInt::Div(Context, IntNumerator, IntDenominator, &bHasNonZeroRemainder);
	if (bHasNonZeroRemainder && (IntNumerator.IsNegative() == IntDenominator.IsNegative()))
	{
		IntQuotient = VInt::Add(Context, IntQuotient, VInt(1));
	}
	return IntQuotient;
}

void VRational::Reduce(FAllocationContext Context)
{
	if (bIsReduced)
	{
		return;
	}

	VInt A = Numerator.Get();
	VInt B = Denominator.Get();
	while (!VInt::Eq(Context, B, VInt(0)))
	{
		VInt Remainder = VInt::Mod(Context, A, B);
		A = B;
		B = Remainder;
	}

	VInt NewNumerator = VInt::Div(Context, Numerator.Get(), A);
	VInt NewDenominator = VInt::Div(Context, Denominator.Get(), A);

	Numerator.Set(Context, NewNumerator);
	Denominator.Set(Context, NewDenominator);
	bIsReduced = true;
}

void VRational::NormalizeSigns(FAllocationContext Context)
{
	VInt Denom = Denominator.Get();
	if (VInt::Lt(Context, Denom, VInt(0)))
	{
		// The denominator is < 0, so we need to normalize the signs
		VInt NewNumerator = VInt::Neg(Context, Numerator.Get());
		VInt NewDenominator = VInt::Neg(Context, Denom);

		Numerator.Set(Context, NewNumerator);
		Denominator.Set(Context, NewDenominator);
	}
}

template <typename TVisitor>
void VRational::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Numerator, TEXT("Numerator"));
	Visitor.Visit(Denominator, TEXT("Denominator"));
}

void VRational::SerializeImpl(VRational*& This, FAllocationContext Context, FAbstractVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		VInt ScratchNumerator;
		VInt ScratchDenominator;
		Visitor.Visit(ScratchNumerator, TEXT("Numerator"));
		Visitor.Visit(ScratchDenominator, TEXT("Denominator"));
		This = &VRational::New(Context, ScratchNumerator.AsInt(), ScratchDenominator.AsInt());
	}
	else
	{
		This->VisitReferences(Visitor);
	}
}

bool VRational::EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder)
{
	if (!Other->IsA<VRational>())
	{
		return false;
	}
	return Eq(Context, *this, Other->StaticCast<VRational>());
}

uint32 VRational::GetTypeHashImpl()
{
	if (!bIsReduced)
	{
		// TLS lookup to reduce rationals before hashing
		// FRunningContextPromise PromiseContext;
		FRunningContext Context((FRunningContextPromise()));
		Reduce(Context);
		NormalizeSigns(Context);
	}
	return ::HashCombineFast(GetTypeHash(Numerator.Get()), GetTypeHash(Denominator.Get()));
}

void VRational::ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter)
{
	Numerator.Get().ToString(Builder, Context, Formatter);
	Builder.Append(TEXT(" / "));
	Denominator.Get().ToString(Builder, Context, Formatter);
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)