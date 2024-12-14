// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include <cstdint>
#include <math.h>
#include <string.h>

namespace uLang
{

class CMath
{
public:

    /// Returns higher value in a generic way
    template<typename T>
    ULANG_FORCEINLINE static constexpr T Max(const T A, const T B)
    {
        return (A>=B) ? A : B;
    }

    /// Returns lower value in a generic way
    template<typename T>
    ULANG_FORCEINLINE static constexpr T Min(const T A, const T B)
    {
        return (A<=B) ? A : B;
    }

    /// Clamps X to be between Min and Max, inclusive
    template<typename T>
    ULANG_FORCEINLINE static constexpr T Clamp(const T X, const T Min, const T Max)
    {
        return X < Min ? Min : (X < Max ? X : Max);
    }

    /// Checks if a number is a power of two
    template <class T>
    ULANG_FORCEINLINE static constexpr bool IsPowerOf2(const T X)
    {
        return X > 0 && (X & (X - 1)) == 0;
    }

    /// Computes natural logarithm
    ULANG_FORCEINLINE static float Loge(float Value)
    {
        return ::logf(Value);
    }

    static ULANGCORE_API double Extensionalize(double Value);

    static ULANGCORE_API double ToFloat(int64_t Value);

    // Arithmetic operations in a non-fast-math environment (IEEE compliant).
    // These can (mostly) be removed when callers are guaranteed to not be
    // compiled with fast-math or similar enabled.
    static ULANGCORE_API double FloatAdd(double Left, double Right);
    static ULANGCORE_API double FloatSubtract(double Left, double Right);
    static ULANGCORE_API double FloatMultiply(double Left, double Right);
    static ULANGCORE_API double FloatDivide(double Left, double Right);

    // FP special constants: NaN and infinity.
    ULANG_FORCEINLINE static double ReinterpretInt64AsDouble(uint64_t Int)
    {
        static_assert(sizeof(double) == sizeof(uint64_t));
        double Result;
        memcpy(&Result, &Int, sizeof(double));
        return Result;
    }
    ULANG_FORCEINLINE static double FloatNaN()      { return ReinterpretInt64AsDouble(0x7ff8'0000'0000'0000); }
    ULANG_FORCEINLINE static double FloatInfinity() { return ReinterpretInt64AsDouble(0x7ff0'0000'0000'0000); }

    // Predicates for different FP specials.
    static ULANGCORE_API bool FloatIsFinite(double Value);
    static ULANGCORE_API bool FloatIsInfinite(double Value);
    static ULANGCORE_API bool FloatIsNaN(double Value);

    // We use an ordering relationship different from the default IEEE float
    // ordering (because we require NaNs to compare equal to each other).
    static ULANGCORE_API bool FloatEqual(double Left, double Right);
    static ULANGCORE_API bool FloatLess(double Left, double Right);
    static ULANGCORE_API bool FloatLessEqual(double Left, double Right);

    // The remaining relations can be inferred from the relations above.
    
    ULANG_FORCEINLINE static bool FloatNotEqual(double Left, double Right)
    {
        return !FloatEqual(Left, Right);
    }

    ULANG_FORCEINLINE static bool FloatGreater(double Left, double Right)
    {
        return FloatLess(Right, Left);
    }

    ULANG_FORCEINLINE static bool FloatGreaterEqual(double Left, double Right)
    {
        return FloatLessEqual(Right, Left);
    }

    // Ranking function that turns a double into an int64 that defines a total order
    // compatible with the ordering implied for floats, to be precise
    //
    //   FloatLess(a,b)  =>  FloatRanking(a) < FloatRanking(b)
    //   FloatEqual(a,b) <=> FloatRanking(a) == FloatRanking(b)
    //
    // For FloatLess(a,b), we only have implication in one direction because when a single
    // NaN is involved, the strict "less" ordering relationship is partial. In the total
    // order implied by FloatRanking, NaN compares larger than all other floats. Unlike
    // normal IEEE semantics, NaNs compare equal to each other in our ordering, so for
    // FloatEqual we have full equivalence.
    //
    // FloatRanking can be used directly or as a key for sorted maps and hashes.
    static ULANGCORE_API int64_t FloatRanking(double Value);
};

}
