// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMFloat.h"
#include "Templates/TypeCompatibleBytes.h"
#include <cfloat>
#include <cstdint>
#include <cstring>

namespace Verse
{
VFloat VFloat::Purify() const
{
	// Purify InValue by replacing potentially impure NaNs with the canonical pure NaN.
	return IsNaN() ? NaN() : *this;
}

VFloat operator-(VFloat Operand)
{
	return VFloat(-Operand.Value);
}

VFloat operator+(VFloat Left, VFloat Right)
{
	return VFloat(Left.Value + Right.Value);
}

VFloat operator-(VFloat Left, VFloat Right)
{
	return VFloat(Left.Value - Right.Value);
}

VFloat operator*(VFloat Left, VFloat Right)
{
	return VFloat(Left.Value * Right.Value);
}

VFloat operator/(VFloat Left, VFloat Right)
{
	// NormalizeSignedZero here guarantees that +-0, which are otherwise
	// indistinguishable, produce the same results for division too.
	return VFloat(Left.Value / Right.NormalizeSignedZero().Value);
}

bool VFloat::IsFinite() const
{
	return Value >= -DBL_MAX && Value <= DBL_MAX;
}

bool VFloat::IsInfinite() const
{
	return Value == Infinity().Value || Value == -Infinity().Value;
}

// We have to do our own since the math.h "isnan" might have been
// included with fast-math on, which then makes it not actually work
// even if we turn off fast-math related options via pragmas on
// several compilers.
bool VFloat::IsNaN() const
{
	// IEEE NaNs don't compare equal to anything, not even themselves.
	return Value != Value;
}

// Used to turn -0 into +0. We want the two representations of
// zero to act as indistinguishable, and this is the easiest way to
// get a canonical representation. Other values are unaffected.
VFloat VFloat::NormalizeSignedZero() const
{
	return VFloat(Value + 0.0);
}

// To obey language expectations about equality and ordering relations,
// we define our own ordering relation on top of the partial ordering implied
// by IEEE floating point.
//
// The existing ordering relationships between finite numbers and infinities
// remain intact, but additionally (and different from IEEE floating point
// semantics), we require that NaNs compare equal to each other, to maintain
// reflexivity of the equality relation.

bool operator==(VFloat Left, VFloat Right)
{
	if (Left.IsNaN())
	{
		return Right.IsNaN();
	}

	// Left is non-NaN. Right might be NaN, in which case it will compare as
	// non-equal which is our desired result. Otherwise the two elements
	// are ordered and we're in the easy case anyway.
	return Left.Value == Right.Value;
}

bool operator<(VFloat Left, VFloat Right)
{
	// Regular floating-point compare for less is sufficient here:
	// when either Left or Right is NaN, the resulting compare is
	// unordered, hence Less(Left,Right) is false, which is correct.
	return Left.Value < Right.Value;
}

bool operator<=(VFloat Left, VFloat Right)
{
	// If Left is a NaN, we are definitely not less, but we might be equal if
	// Right is also a NaN.
	if (Left.IsNaN())
	{
		return Right.IsNaN();
	}

	// If Left is non-NaN and Right is non-NaN, a regular <= compare suffices.
	// If Left is non-NaN and Right is NaN, we are neither less nor equal by
	// our relation; a regular <= compare returns false in this case, which
	// matches our definition.
	return Left.Value <= Right.Value;
}

int64 VFloat::Ranking() const
{
	// NaNs compare as more positive than anything else in our total order
	if (IsNaN())
	{
		return INT64_MAX;
	}

	// Bit-cast to get the underlying representation
	int64 FloatBits;
	static_assert(sizeof(Value) == sizeof(FloatBits), "Double size should match int64_t");
	memcpy(&FloatBits, &Value, sizeof(Value));

	// Values with sign bit clear can represent themselves, negative values
	// are sign-magnitude and need to be converted to two's complement.
	// Can be done branch-less if desired.
	if (FloatBits >= 0)
	{
		return FloatBits;
	}
	else
	{
		// To go from sign-magnitude to two's complement on signed values,
		// complement the value bits then add 1.
		return (FloatBits ^ 0x7fff'ffff'ffff'ffff) + 1;
	}
}
} // namespace Verse

#endif // WITH_VERSE_VM