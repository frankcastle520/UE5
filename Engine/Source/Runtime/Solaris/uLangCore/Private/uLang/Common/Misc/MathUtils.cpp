// Copyright Epic Games, Inc. All Rights Reserved.

// All this pragma business needs to go before the very first header included!

#if defined(__clang__)

// Clang needs to go first since it can self-report as either __GNUC__ or _MSC_VER depending on target
// and implements neither compiler's floating-point pragmas the way the original compilers do. Sigh.
#if defined(__FAST_MATH__) || __FINITE_MATH_ONLY__ > 0

// If we're in fastmath mode currently, we have to opt into precise float semantics
// Clang 11+ (in official numbering) provides a way to do this. I don't know what version
// of Apple Clang this corresponds to, and there is no feature check test for it, so
// version numbers it is for now.
#if __clang_major__ >= 11

#define CLANG_FLOAT_CONTROL_PUSHED

// Clang has float_control like MSVC does
#pragma float_control(precise, on, push)

#else

#error Clang in fast-math mode with no way to turn on precise mode; this is not allowed!

#endif // __clang_major__ >= 11

#endif // fast-math or finite-math-only

// Clang respects the standard fp_contract pragma, but has no way to push status (like MSVC).
// Also, as of this writing there seems to be a bug in Clang 14.0 (and several older versions as well)
// where disabling contractions doesn't work right if -ffast-math was specified on the compiler command
// line (probably related to the implied -ffp-contract=fast, but this is not verified). There's not
// much we can do about this case for now.
#pragma STDC FP_CONTRACT OFF

#elif defined(__GNUC__)

// These settings are used for GCC-based compilers as well as compilers pretending
// to be GCC such as Clang in GCC mode.

#pragma GCC push_options
#pragma GCC optimize("-fno-fast-math")
#pragma GCC optimize("-ffp-contract=off")

#elif defined(_MSC_VER)

#pragma float_control(precise, on, push)
// Unfortunately this option does not have a "push" setting so we don't know what it was beforehand; we just
// leave it off even in PreciseFloatsPop.
#pragma fp_contract(off)

#else

#error Compiler not recognized!

#endif

#include "uLang/Common/Misc/MathUtils.h"
#include <string.h>
#include <cfloat>

namespace uLang
{

// Used to turn -0 into +0. We want the two to representations of
// zero to act as indistinguishable, and this is the easiest way to
// get a canonical representation. Other values are unaffected.
double CMath::Extensionalize(double Value)
{
    return Value + 0.0;
}

double CMath::ToFloat(int64_t Value)
{
    return (double)Value;
}

// Binary arithmetic operators.
double CMath::FloatAdd(double Left, double Right)
{
    return Left + Right;
}

double CMath::FloatSubtract(double Left, double Right)
{
    return Left - Right;
}

double CMath::FloatMultiply(double Left, double Right)
{
    return Left * Right;
}

double CMath::FloatDivide(double Left, double Right)
{
    // Extensionalize here guarantees that +-0, which are otherwise
    // indistinguishable, produce the same results for division too.
    return Left / Extensionalize(Right);
}

bool CMath::FloatIsFinite(double Value)
{
    return Value >= -DBL_MAX && Value <= DBL_MAX;
}

bool CMath::FloatIsInfinite(double Value)
{
    return Value == FloatInfinity() || Value == -FloatInfinity();
}

// We have to do our own since the math.h "isnan" might have been
// included with fast-math on, which then makes it not actually work
// even if we turn off fast-math related options via pragmas on
// several compilers.
bool CMath::FloatIsNaN(double Value)
{
    // IEEE NaNs don't compare equal to anything, not even themselves.
    return Value != Value;
}

// To obey language expectations about equality and ordering relations,
// we define our own ordering relation on top of the partial ordering implied
// by IEEE floating point.
//
// The existing ordering relationships between finite numbers and infinities
// remain intact, but additionally (and different from IEEE floating point
// semantics), we require that NaNs compare equal to each other, to maintain
// reflexivity of the equality relation.

bool CMath::FloatEqual(double Left, double Right)
{
    if (FloatIsNaN(Left))
    {
        return FloatIsNaN(Right);
    }

    // Left is non-NaN. Right might be NaN, in which case it will compare as
    // non-equal which is our desired result. Otherwise the two elements
    // are ordered and we're in the easy case anyway.
    return Left == Right;
}

bool CMath::FloatLess(double Left, double Right)
{
    // Regular floating-point compare for less is sufficient here:
    // when either Left or Right is NaN, the resulting compare is
    // unordered, hence Less(Left,Right) is false, which is correct.
    return Left < Right;
}

bool CMath::FloatLessEqual(double Left, double Right)
{
    // If Left is a NaN, we are definitely not less, but we might be equal if
    // Right is also a NaN.
    if (FloatIsNaN(Left))
    {
        return FloatIsNaN(Right);
    }

    // If Left is non-NaN and Right is non-NaN, a regular <= compare suffices.
    // If Left is non-NaN and Right is NaN, we are neither less nor equal by
    // our relation; a regular <= compare returns false in this case, which
    // matches our definition.
    return Left <= Right;
}

int64_t CMath::FloatRanking(double Value)
{
    // NaNs compare as more positive than anything else in our total order
    if (FloatIsNaN(Value))
    {
        return INT64_MAX;
    }

    // Bit-cast to get the underlying representation
    int64_t FloatBits;
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

}

#if defined(__clang__)

#ifdef CLANG_FLOAT_CONTROL_PUSHED

#pragma float_control(pop)
#undef CLANG_FLOAT_CONTROL_PUSHED

#endif

#elif defined(__GNUC__)

#pragma GCC pop_options

#elif defined(_MSC_VER)

#pragma float_control(pop)

#else

#error Compiler not recognized!

#endif
