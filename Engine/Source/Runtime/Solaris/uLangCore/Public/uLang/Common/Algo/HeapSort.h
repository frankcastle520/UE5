// Copyright Epic Games, Inc. All Rights Reserved.
// Interface and implementation of heap sort

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Templates/Sorting.h"
#include "uLang/Common/Templates/Invoke.h"
#include "uLang/Common/Templates/Storage.h" // for Swap()

namespace uLang
{

namespace AlgoImpl
{
    /**
     * Gets the index of the left child of node at Index.
     *
     * @param   Index Node for which the left child index is to be returned.
     * @returns Index of the left child.
     */
    ULANG_FORCEINLINE int32_t HeapGetLeftChildIndex(int32_t Index)
    {
        return Index * 2 + 1;
    }

    /**
     * Checks if node located at Index is a leaf or not.
     *
     * @param   Index Node index.
     * @returns true if node is a leaf, false otherwise.
     */
    ULANG_FORCEINLINE bool HeapIsLeaf(int32_t Index, int32_t Count)
    {
        return HeapGetLeftChildIndex(Index) >= Count;
    }

    /**
     * Gets the parent index for node at Index.
     *
     * @param   Index node index.
     * @returns Parent index.
     */
    ULANG_FORCEINLINE int32_t HeapGetParentIndex(int32_t Index)
    {
        return (Index - 1) / 2;
    }

    /**
     * Fixes a possible violation of order property between node at Index and a child.
     *
     * @param   Heap        Pointer to the first element of a binary heap.
     * @param   Index       Node index.
     * @param   Count       Size of the heap.
     * @param   Projection  The projection to apply to the elements.
     * @param   Predicate   A binary predicate object used to specify if one element should precede another.
     */
    template <typename RangeValueType, typename ProjectionType, typename PredicateType>
    ULANG_FORCEINLINE void HeapSiftDown(RangeValueType* Heap, int32_t Index, const int32_t Count, const ProjectionType& Projection, const PredicateType& Predicate)
    {
        while (!HeapIsLeaf(Index, Count))
        {
            const int32_t LeftChildIndex = HeapGetLeftChildIndex(Index);
            const int32_t RightChildIndex = LeftChildIndex + 1;

            int32_t MinChildIndex = LeftChildIndex;
            if (RightChildIndex < Count)
            {
                MinChildIndex = Predicate( uLang::Invoke(Projection, Heap[LeftChildIndex]), uLang::Invoke(Projection, Heap[RightChildIndex]) ) ? LeftChildIndex : RightChildIndex;
            }

            if (!Predicate( uLang::Invoke(Projection, Heap[MinChildIndex]), uLang::Invoke(Projection, Heap[Index]) ))
            {
                break;
            }

            uLang::Swap(Heap[Index], Heap[MinChildIndex]);
            Index = MinChildIndex;
        }
    }

    /**
     * Fixes a possible violation of order property between node at NodeIndex and a parent.
     *
     * @param   Heap        Pointer to the first element of a binary heap.
     * @param   RootIndex   How far to go up?
     * @param   NodeIndex   Node index.
     * @param   Projection  The projection to apply to the elements.
     * @param   Predicate   A binary predicate object used to specify if one element should precede another.
     *
     * @return  The new index of the node that was at NodeIndex
     */
    template <class RangeValueType, typename ProjectionType, class PredicateType>
    ULANG_FORCEINLINE int32_t HeapSiftUp(RangeValueType* Heap, int32_t RootIndex, int32_t NodeIndex, const ProjectionType& Projection, const PredicateType& Predicate)
    {
        while (NodeIndex > RootIndex)
        {
            int32_t ParentIndex = HeapGetParentIndex(NodeIndex);
            if (!Predicate( uLang::Invoke(Projection, Heap[NodeIndex]), uLang::Invoke(Projection, Heap[ParentIndex]) ))
            {
                break;
            }

            uLang::Swap(Heap[NodeIndex], Heap[ParentIndex]);
            NodeIndex = ParentIndex;
        }

        return NodeIndex;
    }

    /**
     * Builds an implicit min-heap from a range of elements.
     * This is the internal function used by Heapify overrides.
     *
     * @param   First       pointer to the first element to heapify
     * @param   Num         the number of items to heapify
     * @param   Projection  The projection to apply to the elements.
     * @param   Predicate   A binary predicate object used to specify if one element should precede another.
     */
    template <typename RangeValueType, typename ProjectionType, typename PredicateType>
    ULANG_FORCEINLINE void HeapifyInternal(RangeValueType* First, int32_t Num, ProjectionType Projection, PredicateType Predicate)
    {
        for (int32_t Index = HeapGetParentIndex(Num - 1); Index >= 0; Index--)
        {
            HeapSiftDown(First, Index, Num, Projection, Predicate);
        }
    }

    /**
     * Performs heap sort on the elements.
     * This is the internal sorting function used by HeapSort overrides.
     *
     * @param   First       pointer to the first element to sort
     * @param   Num         the number of elements to sort
     * @param   Predicate   predicate class
     */
    template <typename RangeValueType, typename ProjectionType, class PredicateType>
    void HeapSortInternal(RangeValueType* First, int32_t Num, ProjectionType Projection, PredicateType Predicate)
    {
        TReversePredicate< PredicateType > ReversePredicateWrapper(Predicate); // Reverse the predicate to build a max-heap instead of a min-heap
        HeapifyInternal(First, Num, Projection, ReversePredicateWrapper);

        for(int32_t Index = Num - 1; Index > 0; Index--)
        {
            uLang::Swap(First[0], First[Index]);

            HeapSiftDown(First, 0, Index, Projection, ReversePredicateWrapper);
        }
    }
}

namespace Algo
{
    /**
     * Performs heap sort on the elements. Assumes < operator is defined for the element type.
     *
     * @param Range     The range to sort.
     */
    template <typename RangeType>
    ULANG_FORCEINLINE void HeapSort(RangeType& Range)
    {
        AlgoImpl::HeapSortInternal(ULangGetData(Range), ULangGetNum(Range), SIdentityFunctor(), TLess<>());
    }

    /**
     * Performs heap sort on the elements.
     *
     * @param Range     The range to sort.
     * @param Predicate A binary predicate object used to specify if one element should precede another.
     */
    template <typename RangeType, typename PredicateType>
    ULANG_FORCEINLINE void HeapSort(RangeType& Range, PredicateType Predicate)
    {
        AlgoImpl::HeapSortInternal(ULangGetData(Range), ULangGetNum(Range), SIdentityFunctor(), uLang::Move(Predicate));
    }

    /**
     * Performs heap sort on the elements. Assumes < operator is defined for the projected element type.
     *
     * @param Range     The range to sort.
     * @param Projection    The projection to sort by when applied to the element.
     */
    template <typename RangeType, typename ProjectionType>
    ULANG_FORCEINLINE void HeapSortBy(RangeType& Range, ProjectionType Projection)
    {
        AlgoImpl::HeapSortInternal(ULangGetData(Range), ULangGetNum(Range), uLang::Move(Projection), TLess<>());
    }

    /**
     * Performs heap sort on the elements.
     *
     * @param Range     The range to sort.
     * @param Projection    The projection to sort by when applied to the element.
     * @param Predicate A binary predicate object, applied to the projection, used to specify if one element should precede another.
     */
    template <typename RangeType, typename ProjectionType, typename PredicateType>
    ULANG_FORCEINLINE void HeapSortBy(RangeType& Range, ProjectionType Projection, PredicateType Predicate)
    {
        AlgoImpl::HeapSortInternal(ULangGetData(Range), ULangGetNum(Range), uLang::Move(Projection), uLang::Move(Predicate));
    }
}
}