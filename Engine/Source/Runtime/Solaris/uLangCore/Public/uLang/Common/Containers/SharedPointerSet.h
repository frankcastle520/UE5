// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/PointerSetHelper.h"
#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Misc/Optional.h"

namespace uLang
{

/**
 * Templated dynamic set of shared pointers to elements
 * This is similar to TSPtrArrayG, plus elements are always kept in sorted order and looked up via binary search
 **/
template<typename InElementType, bool AllowNull, typename InKeyType, typename InElementAllocatorType, typename... RawAllocatorArgsType>
class TSPtrSetG
    : protected TSPtrArrayG<InElementType, AllowNull, InElementAllocatorType, RawAllocatorArgsType...>
{
    template <typename OtherElementType, bool OtherAllowNull, typename OtherKeyType, typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    friend class TSPtrSetG;

    using Super = TSPtrArrayG<InElementType, AllowNull, InElementAllocatorType, RawAllocatorArgsType...>;
    using Super::_PointerStorage;
    using Helper = TPointerSetHelper<InElementType, InKeyType>;

public:

    using KeyType = InKeyType;
    using typename Super::ElementType;
    using typename Super::PointerType;
    using Super::EnableDereference;

    /**
     * Constructor
     */
    ULANG_FORCEINLINE TSPtrSetG(RawAllocatorArgsType&&... RawAllocatorArgs)
        : Super(uLang::ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {}

    /**
     * Copy constructor.
     *
     * @param Other The source array to copy.
     */
    ULANG_FORCEINLINE TSPtrSetG(const TSPtrSetG & Other)
        : Super(Other)
    {
    }

    /**
     * Copy constructor.
     *
     * @param Other The source array to copy.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    ULANG_FORCEINLINE TSPtrSetG(const TSPtrSetG & Other, int32_t ExtraSlack)
        : Super(Other, ExtraSlack)
    {
    }

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     */
    ULANG_FORCEINLINE TSPtrSetG(TSPtrSetG && Other)
        : Super(ForwardArg<Super>(Other))
    {
    }

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     * @param ExtraSlack Tells how much extra pointer memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    TSPtrSetG(TSPtrSetG && Other, int32_t ExtraSlack)
        : Super(ForwardArg<Super>(Other), ExtraSlack)
    {
    }

    /**
     * Assignment operators.
     */
    TSPtrSetG& operator=(const TSPtrSetG& Other) { Super::operator=(Other); return *this; }
    TSPtrSetG& operator=(TSPtrSetG&& Other) { Super::operator=(Move(Other)); return *this; }

    // Use these superclass methods as-is:
    using Super::GetSlack;
    using Super::IsValidIndex;
    using Super::Num;
    using Super::Max;
    using Super::IsEmpty;
    using Super::IsFilled;
    using Super::operator[];
    using Super::FindByPredicate;
    using Super::IndexOfByPredicate;
    using Super::Shrink;
    using Super::RemoveAt;
    using Super::ReplaceAt;
    using Super::Reset;
    using Super::Empty;
    using Super::Reserve;
    using typename Super::Iterator;
    using Super::begin;
    using Super::end;

    /**
     * Finds an item by key (assuming the ElementType overloads operator< and operator== for
     * the comparison).
     *
     * @param Key The key to search by.
     * @returns Index to the first matching element, or IndexNone if none is found.
     */
    int32_t IndexOf(const KeyType& Key) const
    {
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, Key);
        return (UpperBound < Num() && KeyType(*_PointerStorage[UpperBound]) == Key) ? UpperBound : IndexNone;
    }

    /**
     * Finds an item by key (assuming the ElementType overloads operator== for
     * the comparison).
     *
     * @param Key The key to search by.
     * @returns Pointer to the first matching element, or nullptr if none is found.
     * @see Find
     */
    ULANG_FORCEINLINE TOptional<PointerType> Find(const KeyType& Key) const
    {
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, Key);
        if (UpperBound < Num())
        {
            ElementType* Item = _PointerStorage[UpperBound];
            if (KeyType(*Item) == Key)
            {
                return PointerType(Item, _PointerStorage.GetRawAllocator());
            }
        }
        return TOptional<PointerType>();
    }

    /**
     * Checks if this array contains the element.
     *
     * @returns True if found. False otherwise.
     * @see ContainsByPredicate, FilterByPredicate, FindByPredicate
     */
    bool Contains(const KeyType& Key) const
    {
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, Key);
        return UpperBound < Num() && KeyType(*_PointerStorage[UpperBound]) == Key;
    }

    /**
     * Appends the specified array to this array.
     *
     * Allocator changing version.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    void Append(const TSPtrSetG& Other)
    {
        Other.ReferenceAll();
        Merge(_PointerStorage, Other._PointerStorage);
        EnableDereference();
    }

    /**
     * Appends the specified array to this array.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    void Append(TSPtrSetG&& Other)
    {
        Merge(_PointerStorage, Other._PointerStorage);
        Other.Empty();
        EnableDereference();
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * Move semantics version.
     *
     * @param Other The array to append.
     */
    TSPtrSetG& operator+=(TSPtrSetG&& Other)
    {
        Append(ForwardArg<TSPtrSetG>(Other));
        return *this;
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * @param Other The array to append.
     */
    TSPtrSetG& operator+=(const TSPtrSetG& Other)
    {
        Append(Other);
        return *this;
    }

    /**
     * Constructs a new item at the end of the array, possibly reallocating the whole array to fit.
     *
     * @param Args  The arguments to forward to the constructor of the new item.
     * @return      Index to the new item
     */
    template <typename... CtorArgsType>
    ULANG_FORCEINLINE int32_t AddNew(CtorArgsType&&... CtorArgs)
    {
        ElementType * Item = new(_PointerStorage.GetRawAllocator()) ElementType(uLang::ForwardArg<CtorArgsType>(CtorArgs)...);
        Item->Reference();
        EnableDereference();
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, *Item);
        ULANG_ASSERTF(UpperBound == Num() || KeyType(*_PointerStorage[UpperBound]) != KeyType(*Item), "Tried to add duplicate item!");
        _PointerStorage.EmplaceAt(UpperBound, Item);
        return UpperBound;
    }

    /**
     * Adds a new item. Must not exist yet.
     *
     * Move semantics version.
     *
     * @param Item The item to add
     * @return Index to the new item
     * @see AddDefaulted, AddUnique, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t Add(const PointerType& Item)
    {
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, *Item);
        ULANG_ASSERTF(UpperBound == Num() || KeyType(*_PointerStorage[UpperBound]) != KeyType(*Item), "Tried to add duplicate item!");
        return Super::Insert(Item, UpperBound);
    }

    /**
     * Removes as many instances of Item as there are in the array, maintaining
     * order but not indices.
     *
     * @param Item Item to remove from array.
     * @returns Number of removed elements.
     * @see Add, Insert, RemoveAll, RemoveAllSwap, RemoveSingle, RemoveSwap
     */
    int32_t Remove(const KeyType& Key)
    {
        int32_t Count = 0;
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, Key);
        if (UpperBound < Num() && KeyType(*_PointerStorage[UpperBound]) == Key)
        {
            Super::RemoveAt(UpperBound);
            ++Count;
        }
        ULANG_ASSERTF(UpperBound == Num() || KeyType(*_PointerStorage[UpperBound]) != Key, "Matching item still present after Remove()!");
        return Count;
    }

};

/// Set of shared pointers that allocates elements on the heap
template<typename ElementType, typename KeyType>
using TSPtrSet = TSPtrSetG<ElementType, true, KeyType, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Set of shared references that allocates elements on the heap
template<typename ElementType, typename KeyType>
using TSRefSet = TSPtrSetG<ElementType, false, KeyType, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Set of shared pointers that allocates object using a given allocator instance
template<typename ElementType, typename KeyType>
using TSPtrSetA = TSPtrSetG<ElementType, true, KeyType, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;

/// Set of shared references that allocates object using a given allocator instance
template<typename ElementType, typename KeyType>
using TSRefSetA = TSPtrSetG<ElementType, false, KeyType, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;


template <typename ElementType, bool AllowNull, typename KeyType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsZeroConstructType<TSPtrSetG<ElementType, AllowNull, KeyType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = TAllocatorTraits<ElementAllocatorType>::IsZeroConstruct };
};

template <typename ElementType, bool AllowNull, typename KeyType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TContainerTraits<TSPtrSetG<ElementType, AllowNull, KeyType, ElementAllocatorType, RawAllocatorArgsType...>> : public TContainerTraitsBase<TSPtrSetG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    static_assert(TAllocatorTraits<ElementAllocatorType>::SupportsMove, "TSPtrArray no longer supports move-unaware allocators");
    enum { MoveWillEmptyContainer = TAllocatorTraits<ElementAllocatorType>::SupportsMove };
};

template <typename ElementType, bool AllowNull, typename KeyType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsContiguousContainer<TSPtrSetG<ElementType, AllowNull, KeyType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = true };
};

}
