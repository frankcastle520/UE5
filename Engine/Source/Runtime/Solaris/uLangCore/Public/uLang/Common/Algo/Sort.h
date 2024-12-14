// Copyright Epic Games, Inc. All Rights Reserved.
// Generic sorting interface

#pragma once

#include "uLang/Common/Algo/IntroSort.h"

namespace uLang
{
namespace Algo
{
    /**
     * Sort a range of elements using its operator<.  The sort is unstable.
     *
     * @param  Range  The range to sort.
     */
    template <typename RangeType>
    ULANG_FORCEINLINE void Sort(RangeType&& Range)
    {
        IntroSort(uLang::ForwardArg<RangeType>(Range));
    }

    /**
     * Sort a range of elements using a user-defined predicate class.  The sort is unstable.
     *
     * @param  Range      The range to sort.
     * @param  Predicate  A binary predicate object used to specify if one element should precede another.
     */
    template <typename RangeType, typename PredicateType>
    ULANG_FORCEINLINE void Sort(RangeType&& Range, PredicateType Pred)
    {
        IntroSort(uLang::ForwardArg<RangeType>(Range), uLang::Move(Pred));
    }

    /**
     * Sort a range of elements by a projection using the projection's operator<.  The sort is unstable.
     *
     * @param  Range  The range to sort.
     * @param  Proj   The projection to sort by when applied to the element.
     */
    template <typename RangeType, typename ProjectionType>
    ULANG_FORCEINLINE void SortBy(RangeType&& Range, ProjectionType Proj)
    {
        IntroSortBy(uLang::ForwardArg<RangeType>(Range), uLang::Move(Proj));
    }

    /**
     * Sort a range of elements by a projection using a user-defined predicate class.  The sort is unstable.
     *
     * @param  Range      The range to sort.
     * @param  Proj       The projection to sort by when applied to the element.
     * @param  Predicate  A binary predicate object, applied to the projection, used to specify if one element should precede another.
     */
    template <typename RangeType, typename ProjectionType, typename PredicateType>
    ULANG_FORCEINLINE void SortBy(RangeType&& Range, ProjectionType Proj, PredicateType Pred)
    {
        IntroSortBy(uLang::ForwardArg<RangeType>(Range), uLang::Move(Proj), uLang::Move(Pred));
    }
}
}