// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Misc/Optional.h"
#include <cstdint>
#include <cfloat>
#include <math.h>

namespace uLang
{
constexpr int32_t Int32Min = INT32_MIN;
constexpr int32_t Int32Max = INT32_MAX;
constexpr uint32_t Int32MaxMagnitude = ((uint32_t)(INT32_MAX))+1;
constexpr uint32_t UInt32Max = UINT32_MAX;

constexpr int64_t Int64Min = INT64_MIN;
constexpr int64_t Int64Max = INT64_MAX;
constexpr uint64_t Int64MaxMagnitude = ((uint64_t)(INT64_MAX))+1;
constexpr uint64_t UInt64Max = UINT64_MAX;

constexpr float Float32Min = FLT_MIN;
constexpr float Float32Max = FLT_MAX;
constexpr double Float64Min = DBL_MIN;
constexpr double Float64Max = DBL_MAX;

//
// todo: implement the rest of the types (int32_t, int16_t, ...), and then also 
//       provide platform specific optimized versions of this. in particular, 
//       platforms  that use the Clang and GCC compilers have intrinsics 
//       available such as '__builtin_add_overflow' that will perform the operation 
//       and return whether or not overflow happened, which should yield more
//       optimal code.
//
constexpr bool CheckedI64Negate(int64_t Rhs, int64_t* OutResult)
{
    const bool bDidOverflow = (Rhs == Int64Min);
    *OutResult = static_cast<int64_t>(~static_cast<uint64_t>(Rhs) + 1);
    return !bDidOverflow;
}

constexpr bool CheckedI64Abs(int64_t Rhs, int64_t* OutResult)
{
    int64_t Result = 0;
    const bool bDidOverflow = (Rhs == Int64Min);

	if (!bDidOverflow)
	{
		Result = (Rhs >= 0) ? Rhs : -Rhs;
	}

	*OutResult = Result;
	return !bDidOverflow;
}

constexpr bool CheckedConvertI32I64(int64_t Rhs, int32_t* OutResult)
{
    const int32_t Result = (int32_t)Rhs;
    const bool bDidOverflow = (Rhs < Int32Min) || (Rhs > Int32Max);

	*OutResult = Result;
	return !bDidOverflow;
}

constexpr bool CheckedI64Add(int64_t Lhs, int64_t Rhs, int64_t* OutResult)
{
	// note it is important per the C++ spec to do the addition
	// as unsigned, which is well defined to work as 'modulo 2^64'
	// in the face of overflow, whereas signed addition is assumed
	// to not overflow and invokes undefined behavior
    const int64_t Result = (int64_t)((uint64_t)Lhs + (uint64_t)Rhs);

    const bool bDidOverflow = ((Rhs >= 0) && (Result < Lhs)) ||
	                          ((Lhs < 0) && (Result > Rhs));

	*OutResult = Result;
	return !bDidOverflow;
}

constexpr bool CheckedI64Subtract(int64_t Lhs, int64_t Rhs, int64_t* OutResult)
{
    const int64_t Result = (int64_t)((uint64_t)Lhs - (uint64_t)Rhs);

    const bool bDidOverflow = ((Rhs < 0) && (Result < Lhs)) ||
	                          ((Rhs >= 0) && (Result > Lhs));

	*OutResult = Result;
	return !bDidOverflow;
}

constexpr bool CheckedI64Multiply(int64_t Lhs, int64_t Rhs, int64_t* OutResult)
{
	// do it as sign magnitude
    const bool bIsLhsNegative = Lhs < 0;
    const bool bIsRhsNegative = Rhs < 0;
    const bool bIsResultNegative = bIsLhsNegative ^ bIsRhsNegative;

    const uint64_t LhsMagnitude = bIsLhsNegative ? ~static_cast<uint64_t>(Lhs) + 1 : (uint64_t)Lhs;
    const uint64_t RhsMagnitude = bIsRhsNegative ? ~static_cast<uint64_t>(Rhs) + 1 : (uint64_t)Rhs;

    const uint64_t LhsLow = (uint64_t)(LhsMagnitude & 0xFFFFFFFF);
    const uint64_t LhsHigh = (uint64_t)(LhsMagnitude >> 32);

	const uint64_t RhsLow = (uint64_t)(RhsMagnitude & 0xFFFFFFFF);
	const uint64_t RhsHigh = (uint64_t)(RhsMagnitude >> 32);

	const uint64_t LowLow = LhsLow * RhsLow;
	const uint64_t HighLow = LhsHigh * RhsLow;
	const uint64_t LowHigh = LhsLow * RhsHigh;
	const uint64_t HighHigh = LhsHigh * RhsHigh;

	// do long addition of the parts discarding the 
    // irrelevant lower bits because we are only 
    // interested in detecting whether the solution 
    // overflows or not
	uint64_t DetectOverflow = (LowLow >> 32);
	DetectOverflow += HighLow;
	DetectOverflow += LowHigh;
	DetectOverflow >>= 32;
	DetectOverflow += HighHigh;

	// assuming it does fit in 64 bits, this is the magnitude...
    const uint64_t SolveLowMagnitude = LhsMagnitude*RhsMagnitude;

	if (bIsResultNegative && (SolveLowMagnitude > 9223372036854775808ULL))
	{
		DetectOverflow = 1;
	}
	else if (!bIsResultNegative && (SolveLowMagnitude > 9223372036854775807ULL))
	{
		DetectOverflow = 1;
	}

    const int64_t Result = bIsResultNegative ? ~SolveLowMagnitude + 1 : SolveLowMagnitude;

	*OutResult = Result;
	return (DetectOverflow == 0);
}

constexpr bool CheckedI64Divide(int64_t Lhs, int64_t Rhs, int64_t* OutResult)
{
    int64_t Result = 0;

	const bool bDidOverflow = (Lhs == Int64Min) && (Rhs == -1LL);

	if (!bDidOverflow)
	{
		Result = Lhs / Rhs;
	}

	*OutResult = Result;
	return !bDidOverflow;
}

constexpr bool CheckedI64Modulo(int64_t Lhs, int64_t Rhs, int64_t* OutResult)
{
    int64_t Result = 0;

	const bool bDidOverflow = (Lhs == Int64Min) && (Rhs == -1LL);

	if (!bDidOverflow)
	{
		Result = Lhs % Rhs;
	}

	*OutResult = Result;
	return !bDidOverflow;
}

constexpr bool IsFactor(int64_t Left, int64_t Right) {
    return !(Right % Left);
}

constexpr bool SameSign(int64_t Left, int64_t Right) {
    return (Left ^ Right) >= 0;
}

inline TOptional<int64_t> CheckedI64DivideAndRoundUp(int64_t Left, int64_t Right) {
    if (Left == Int64Min && Right == -1)
    {
        return {};
    }
    bool bIsNotFactor = !IsFactor(Right, Left);
    bool bSameSign = SameSign(Left, Right);
    return (Left / Right) + (bIsNotFactor & bSameSign);
}

inline TOptional<int64_t> CheckedI64DivideAndRoundDown(int64_t Left, int64_t Right) {
    if (Left == Int64Min && Right == -1)
    {
        return {};
    }
    bool bIsNotFactor = !IsFactor(Right, Left);
    bool bNotSameSign = !SameSign(Left, Right);
    return (Left / Right) - (bIsNotFactor & bNotSameSign);
}
}
