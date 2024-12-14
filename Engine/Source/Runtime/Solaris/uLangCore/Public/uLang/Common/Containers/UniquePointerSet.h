// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/PointerSetHelper.h"
#include "uLang/Common/Containers/UniquePointerArray.h"

namespace uLang
{

/**
 * Templated dynamic set of shared pointers to elements
 * This is similar to TUPtrArrayG, plus elements are always kept in sorted order and looked up via binary search
 **/
template<typename InElementType, bool AllowNull, typename InKeyType, typename InElementAllocatorType, typename... RawAllocatorArgsType>
class TUPtrSetG
    : protected TUPtrArrayG<InElementType, AllowNull, InElementAllocatorType, RawAllocatorArgsType...>
{
    template <typename OtherElementType, bool OtherAllowNull, typename OtherKeyType, typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    friend class TUPtrSetG;

    using Super = TUPtrArrayG<InElementType, AllowNull, InElementAllocatorType, RawAllocatorArgsType...>;
    using Super::_PointerStorage;
    using Helper = TPointerSetHelper<InElementType, InKeyType>;

public:

    using KeyType = InKeyType;
    using typename Super::ElementType;
    using typename Super::PointerType;

    /**
     * Constructor
     */
    ULANG_FORCEINLINE TUPtrSetG(RawAllocatorArgsType&&... RawAllocatorArgs)
        : Super(uLang::ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {}

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     */
    ULANG_FORCEINLINE TUPtrSetG(TUPtrSetG && Other)
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
    TUPtrSetG(TUPtrSetG && Other, int32_t ExtraSlack)
        : Super(ForwardArg<Super>(Other), ExtraSlack)
    {
    }

    // Use these superclass methods as-is:
    using Super::operator=;
    using Super::GetSlack;
    using Super::IsValidIndex;
    using Super::Num;
    using Super::Max;
    using Super::IsEmpty;
    using Super::IsFilled;
    using Super::operator[];
    using Super::Shrink;
    using Super::RemoveAt;
    using Super::ReplaceAt;
    using Super::Reset;
    using Super::Empty;
    using Super::Reserve;
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
    ULANG_FORCEINLINE ElementType* Find(const KeyType& Key) const
    {
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, Key);
        if (UpperBound < Num())
        {
            ElementType* Item = _PointerStorage[UpperBound];
            if (KeyType(*Item) == Key)
            {
                return Item;
            }
        }
        return nullptr;
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
    void Append(const TUPtrSetG& Other)
    {
        Other.ReferenceAll();
        Merge(_PointerStorage, Other._PointerStorage);
    }

    /**
     * Appends the specified array to this array.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    void Append(TUPtrSetG&& Other)
    {
        Merge(_PointerStorage, Other._PointerStorage);
        Other.Empty();
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * Move semantics version.
     *
     * @param Other The array to append.
     */
    TUPtrSetG& operator+=(TUPtrSetG&& Other)
    {
        Append(ForwardArg<TUPtrSetG>(Other));
        return *this;
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * @param Other The array to append.
     */
    TUPtrSetG& operator+=(const TUPtrSetG& Other)
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
    ULANG_FORCEINLINE int32_t Add(PointerType&& Item)
    {
        int32_t UpperBound = Helper::GetUpperBound(_PointerStorage, *Item);
        ULANG_ASSERTF(UpperBound == Num() || KeyType(*_PointerStorage[UpperBound]) != KeyType(*Item), "Tried to add duplicate item!");
        return Super::Insert(ForwardArg<PointerType>(Item), UpperBound);
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

/// Set of unique pointers that allocates elements on the heap
template<typename ElementType, typename KeyType>
using TUPtrSet = TUPtrSetG<ElementType, true, KeyType, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Set of unique references that allocates elements on the heap
template<typename ElementType, typename KeyType>
using TURefSet = TUPtrSetG<ElementType, false, KeyType, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Set of unique pointers that allocates object using a given allocator instance
template<typename ElementType, typename KeyType>
using TUPtrSetA = TUPtrSetG<ElementType, true, KeyType, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;

/// Set of unique references that allocates object using a given allocator instance
template<typename ElementType, typename KeyType>
using TURefSetA = TUPtrSetG<ElementType, false, KeyType, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;


template <typename ElementType, bool AllowNull, typename KeyType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsZeroConstructType<TUPtrSetG<ElementType, AllowNull, KeyType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = TAllocatorTraits<ElementAllocatorType>::IsZeroConstruct };
};

template <typename ElementType, bool AllowNull, typename KeyType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TContainerTraits<TUPtrSetG<ElementType, AllowNull, KeyType, ElementAllocatorType, RawAllocatorArgsType...>> : public TContainerTraitsBase<TUPtrSetG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    static_assert(TAllocatorTraits<ElementAllocatorType>::SupportsMove, "TUPtrArray no longer supports move-unaware allocators");
    enum { MoveWillEmptyContainer = TAllocatorTraits<ElementAllocatorType>::SupportsMove };
};

template <typename ElementType, bool AllowNull, typename KeyType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsContiguousContainer<TUPtrSetG<ElementType, AllowNull, KeyType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = true };
};

}
