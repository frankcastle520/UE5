// Copyright Epic Games, Inc. All Rights Reserved.
// Interface and implementation of introspective sort

#pragma once

#include "uLang/Common/Algo/HeapSort.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Templates/Invoke.h"
#include "uLang/Common/Templates/Storage.h" // for Swap()

namespace uLang
{

namespace AlgoImpl
{
    /**
     * Implementation of an introspective sort. Starts with quick sort and switches to heap sort when the iteration depth is too big.
     * The sort is unstable, meaning that the ordering of equal items is not necessarily preserved.
     * This is the internal sorting function used by IntroSort overrides.
     *
     * @param First         pointer to the first element to sort
     * @param Num           the number of items to sort
     * @param Projection    The projection to sort by when applied to the element.
     * @param Predicate     predicate class
     */
    template <typename T, typename ProjectionType, typename PredicateType>
    void IntroSortInternal(T* First, size_t Num, ProjectionType Projection, PredicateType Predicate)
    {
        struct SStack
        {
            T* Min;
            T* Max;
            uint32_t MaxDepth;
        };

        if( Num < 2 )
        {
            return;
        }

        SStack RecursionStack[32]={{First, First+Num-1, (uint32_t)(CMath::Loge((float)Num) * 2.f)}}, Current, Inner;
        for( SStack* StackTop=RecursionStack; StackTop>=RecursionStack; --StackTop ) //-V625
        {
            Current = *StackTop;

        Loop:
            int32_t Count = int32_t(Current.Max - Current.Min + 1);

            if ( Current.MaxDepth == 0 )
            {
                // We're too deep into quick sort, switch to heap sort
                HeapSortInternal( Current.Min, Count, Projection, Predicate );
                continue;
            }

            if( Count <= 8 )
            {
                // Use simple bubble-sort.
                while( Current.Max > Current.Min )
                {
                    T *Max, *Item;
                    for( Max=Current.Min, Item=Current.Min+1; Item<=Current.Max; Item++ )
                    {
                        if( Predicate( uLang::Invoke( Projection, *Max ), uLang::Invoke( Projection, *Item ) ) )
                        {
                            Max = Item;
                        }
                    }
                    uLang::Swap( *Max, *Current.Max-- );
                }
            }
            else
            {
                // Grab middle element so sort doesn't exhibit worst-cast behavior with presorted lists.
                uLang::Swap( Current.Min[Count/2], Current.Min[0] );

                // Divide list into two halves, one with items <=Current.Min, the other with items >Current.Max.
                Inner.Min = Current.Min;
                Inner.Max = Current.Max+1;
                for( ; ; )
                {
                    while( ++Inner.Min<=Current.Max && !Predicate( uLang::Invoke( Projection, *Current.Min ), uLang::Invoke( Projection, *Inner.Min ) ) );
                    while( --Inner.Max> Current.Min && !Predicate( uLang::Invoke( Projection, *Inner.Max ), uLang::Invoke( Projection, *Current.Min ) ) );
                    if( Inner.Min>Inner.Max )
                    {
                        break;
                    }
                    uLang::Swap( *Inner.Min, *Inner.Max );
                }
                uLang::Swap( *Current.Min, *Inner.Max );

                --Current.MaxDepth;

                // Save big half and recurse with small half.
                if( Inner.Max-1-Current.Min >= Current.Max-Inner.Min )
                {
                    if( Current.Min+1 < Inner.Max )
                    {
                        StackTop->Min = Current.Min;
                        StackTop->Max = Inner.Max - 1;
                        StackTop->MaxDepth = Current.MaxDepth;
                        StackTop++;
                    }
                    if( Current.Max>Inner.Min )
                    {
                        Current.Min = Inner.Min;
                        goto Loop;
                    }
                }
                else
                {
                    if( Current.Max>Inner.Min )
                    {
                        StackTop->Min = Inner  .Min;
                        StackTop->Max = Current.Max;
                        StackTop->MaxDepth = Current.MaxDepth;
                        StackTop++;
                    }
                    if( Current.Min+1<Inner.Max )
                    {
                        Current.Max = Inner.Max - 1;
                        goto Loop;
                    }
                }
            }
        }
    }
}

namespace Algo
{
    /**
     * Sort a range of elements using its operator<. The sort is unstable.
     *
     * @param Range The range to sort.
     */
    template <typename RangeType>
    ULANG_FORCEINLINE void IntroSort(RangeType&& Range)
    {
        AlgoImpl::IntroSortInternal(ULangGetData(Range), ULangGetNum(Range), SIdentityFunctor(), TLess<>());
    }

    /**
     * Sort a range of elements using a user-defined predicate class. The sort is unstable.
     *
     * @param Range     The range to sort.
     * @param Predicate A binary predicate object used to specify if one element should precede another.
     */
    template <typename RangeType, typename PredicateType>
    ULANG_FORCEINLINE void IntroSort(RangeType&& Range, PredicateType Predicate)
    {
        AlgoImpl::IntroSortInternal(ULangGetData(Range), ULangGetNum(Range), SIdentityFunctor(), uLang::Move(Predicate));
    }

    /**
     * Sort a range of elements by a projection using the projection's operator<. The sort is unstable.
     *
     * @param Range         The range to sort.
     * @param Projection    The projection to sort by when applied to the element.
     */
    template <typename RangeType, typename ProjectionType>
    ULANG_FORCEINLINE void IntroSortBy(RangeType&& Range, ProjectionType Projection)
    {
        AlgoImpl::IntroSortInternal(ULangGetData(Range), ULangGetNum(Range), uLang::Move(Projection), TLess<>());
    }

    /**
     * Sort a range of elements by a projection using a user-defined predicate class. The sort is unstable.
     *
     * @param Range         The range to sort.
     * @param Projection    The projection to sort by when applied to the element.
     * @param Predicate     A binary predicate object, applied to the projection, used to specify if one element should precede another.
     */
    template <typename RangeType, typename ProjectionType, typename PredicateType>
    ULANG_FORCEINLINE void IntroSortBy(RangeType&& Range, ProjectionType Projection, PredicateType Predicate)
    {
        AlgoImpl::IntroSortInternal(ULangGetData(Range), ULangGetNum(Range), uLang::Move(Projection), uLang::Move(Predicate));
    }
}

}