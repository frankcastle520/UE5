// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Algo/Sort.h"
#include "uLang/Common/Memory/Allocator.h"
#include "uLang/Common/Memory/MemoryOps.h"
#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Templates/Sorting.h"

namespace uLang
{

namespace Private
{
    template <typename FromArrayType, typename ToArrayType>
    struct TCanMoveTArrayPointersBetweenArrayTypes
    {
        using FromAllocatorType = typename FromArrayType::ElementAllocatorType;
        using ToAllocatorType = typename ToArrayType::ElementAllocatorType;
        using FromElementType = typename FromArrayType::ElementType;
        using ToElementType = typename ToArrayType::ElementType;

        enum
        {
            Value =
                TAreTypesEqual<FromAllocatorType, ToAllocatorType>::Value && // Allocators must be equal
                TContainerTraits<FromArrayType>::MoveWillEmptyContainer &&   // A move must be allowed to leave the source array empty
                (
                    TAreTypesEqual         <ToElementType, FromElementType>::Value || // The element type of the container must be the same, or...
                    TIsBitwiseConstructible<ToElementType, FromElementType>::Value    // ... the element type of the source container must be bitwise constructible from the element type in the destination container
                )
        };
    };
}

/**
 * Templated dynamic array
 *
 * A dynamically sized array of typed elements.  Makes the assumption that your elements are relocate-able;
 * i.e. that they can be transparently moved to new memory without a copy constructor.  The main implication
 * is that pointers to elements in the TArrayG may be invalidated by adding or removing other elements to the array.
 * Removal of elements is O(N) and invalidates the indices of subsequent elements.
 *
 * Caution: as noted below some methods are not safe for element types that require constructors.
 *
 **/
template<typename InElementType, typename InElementAllocatorType, typename... RawAllocatorArgsType>
class TArrayG
{
    template <typename OtherElementType, typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    friend class TArrayG;

public:

    using ElementType = InElementType;
    using ElementAllocatorType = InElementAllocatorType;

    /**
     * Constructor with given raw allocator arguments (none required for heap allocator).
     */
    ULANG_FORCEINLINE TArrayG(RawAllocatorArgsType&&... RawAllocatorArgs)
        : _ElementStorage(ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
        , _ArrayNum(0)
        , _ArrayMax(0)
    {}

    /**
     * Constructor with given raw allocator.
     */
    ULANG_FORCEINLINE TArrayG(const typename ElementAllocatorType::RawAllocatorType & RawAllocator)
        : _ElementStorage(RawAllocator)
        , _ArrayNum(0)
        , _ArrayMax(0)
    {}

    /**
     * Constructor from a raw array of elements.
     *
     * @param Ptr   A pointer to an array of elements to copy.
     * @param Count The number of elements to copy from Ptr.
     * @see Append
     */
    ULANG_FORCEINLINE TArrayG(const ElementType* Ptr, int32_t Count, RawAllocatorArgsType&&... RawAllocatorArgs)
        : _ElementStorage(ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {
        ULANG_ASSERTF(Ptr != nullptr || Count == 0, "Attempted to construct array from invalid source data.");

        CopyToEmpty(Ptr, Count, 0, 0);
    }

    ULANG_FORCEINLINE TArrayG(int32_t Count, const ElementType& Value = ElementType(), RawAllocatorArgsType&&... RawAllocatorArgs)
        : _ElementStorage(ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {
        _ArrayNum = Count;
        ResizeForCopy(Count, 0);
        for (ElementType* I = GetData(), * Last = I + Count; I != Last; ++I)
        {
            new (I) ElementType(Value);
        }
    }

    /**
     * Initializer list constructor
     */
    TArrayG(std::initializer_list<InElementType> InitList, RawAllocatorArgsType&&... RawAllocatorArgs)
        : _ElementStorage(ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {
        // This is not strictly legal, as std::initializer_list's iterators are not guaranteed to be pointers, but
        // this appears to be the case on all of our implementations.  Also, if it's not true on a new implementation,
        // it will fail to compile rather than behave badly.
        CopyToEmpty(InitList.begin(), (int32_t)InitList.size(), 0, 0);
    }

    /// @overload This templated version of the constructor above allows for types aliased by using-declarations to also work with it.
    template <typename OtherElementType>
    TArrayG(std::initializer_list<OtherElementType> InitList, RawAllocatorArgsType&&... RawAllocatorArgs) : _ElementStorage(ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {
        CopyToEmpty(InitList.begin(), (int32_t) InitList.size(), 0, 0);
    }

    /**
     * Copy constructor with changed allocator. Use the common routine to perform the copy.
     *
     * @param Other The source array to copy.
     */
    template <typename OtherElementType, typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    ULANG_FORCEINLINE explicit TArrayG(const TArrayG<OtherElementType, OtherElementAllocatorType, OtherRawAllocatorArgsType...>& Other, RawAllocatorArgsType&&... RawAllocatorArgs)
        : _ElementStorage(ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {
        CopyToEmpty(Other.GetData(), Other.Num(), 0, 0);
    }

    /**
     * Copy constructor. Use the common routine to perform the copy.
     *
     * @param Other The source array to copy.
     */
    ULANG_FORCEINLINE TArrayG(const TArrayG& Other)
        : _ElementStorage(Other._ElementStorage.GetRawAllocator())
    {
        CopyToEmpty(Other.GetData(), Other.Num(), 0, 0);
    }

    /**
     * Copy constructor. Use the common routine to perform the copy.
     *
     * @param Other The source array to copy.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    ULANG_FORCEINLINE TArrayG(const TArrayG& Other, int32_t ExtraSlack)
        : _ElementStorage(Other._ElementStorage.GetRawAllocator())
    {
        CopyToEmpty(Other.GetData(), Other.Num(), 0, ExtraSlack);
    }

    /**
     * Assignment operator. First deletes all currently contained elements
     * and then copies from other array.
     *
     * @param Other The source array to assign from.
     */
    TArrayG& operator=(const TArrayG& Other)
    {
        if (this != &Other)
        {
            ULANG_ASSERTF(_ElementStorage.GetRawAllocator() == Other._ElementStorage.GetRawAllocator(), "Currently, can only assign between arrays using the same allocator.");
            DestructElements(GetData(), _ArrayNum);
            CopyToEmpty(Other.GetData(), Other.Num(), _ArrayMax, 0);
        }
        return *this;
    }

private:

    /**
     * Moves or copies array. Depends on the array type traits.
     *
     * This override moves.
     *
     * @param ToArray Array to move into.
     * @param FromArray Array to move from.
     */
    template <typename FromArrayType, typename ToArrayType>
    static ULANG_FORCEINLINE typename TEnableIf<Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopy(ToArrayType& ToArray, FromArrayType& FromArray, int32_t PrevMax)
    {
        ToArray._ElementStorage.MoveToEmpty(FromArray._ElementStorage);

        ToArray  ._ArrayNum = FromArray._ArrayNum;
        ToArray  ._ArrayMax = FromArray._ArrayMax;
        FromArray._ArrayNum = 0;
        FromArray._ArrayMax = 0;
    }

    /**
     * Moves or copies array. Depends on the array type traits.
     *
     * This override copies.
     *
     * @param ToArray Array to move into.
     * @param FromArray Array to move from.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    template <typename FromArrayType, typename ToArrayType>
    static ULANG_FORCEINLINE typename TEnableIf<!Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopy(ToArrayType& ToArray, FromArrayType& FromArray, int32_t PrevMax)
    {
        ToArray._ElementStorage.SetRawAllocator(FromArray._ElementStorage.GetRawAllocator());
        ToArray.CopyToEmpty(FromArray.GetData(), FromArray.Num(), PrevMax, 0);
    }

    /**
     * Moves or copies array. Depends on the array type traits.
     *
     * This override moves.
     *
     * @param ToArray Array to move into.
     * @param FromArray Array to move from.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    template <typename FromArrayType, typename ToArrayType>
    static ULANG_FORCEINLINE typename TEnableIf<Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopyWithSlack(ToArrayType& ToArray, FromArrayType& FromArray, int32_t PrevMax, int32_t ExtraSlack)
    {
        MoveOrCopy(ToArray, FromArray, PrevMax);

        ToArray.Reserve(ToArray._ArrayNum + ExtraSlack);
    }

    /**
     * Moves or copies array. Depends on the array type traits.
     *
     * This override copies.
     *
     * @param ToArray Array to move into.
     * @param FromArray Array to move from.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    template <typename FromArrayType, typename ToArrayType>
    static ULANG_FORCEINLINE typename TEnableIf<!Private::TCanMoveTArrayPointersBetweenArrayTypes<FromArrayType, ToArrayType>::Value>::Type MoveOrCopyWithSlack(ToArrayType& ToArray, FromArrayType& FromArray, int32_t PrevMax, int32_t ExtraSlack)
    {
        ToArray._ElementStorage.SetRawAllocator(FromArray._ElementStorage.GetRawAllocator());
        ToArray.CopyToEmpty(FromArray.GetData(), FromArray.Num(), PrevMax, ExtraSlack);
    }

public:
    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     */
    ULANG_FORCEINLINE TArrayG(TArrayG && Other)
    {
        MoveOrCopy(*this, Other, 0);
    }

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    TArrayG(TArrayG && Other, int32_t ExtraSlack)
    {
        // We don't implement move semantics for general OtherAllocators, as there's no way
        // to tell if they're compatible with the current one.  Probably going to be a pretty
        // rare requirement anyway.

        MoveOrCopyWithSlack(*this, Other, 0, ExtraSlack);
    }

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     */
    template <typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    ULANG_FORCEINLINE explicit TArrayG(TArrayG<ElementType, OtherElementAllocatorType, OtherRawAllocatorArgsType...>&& Other)
    {
        MoveOrCopy(*this, Other, 0);
    }

    /**
     * Move assignment operator.
     *
     * @param Other Array to assign and move from.
     */
    TArrayG& operator=(TArrayG && Other)
    {
        if (this != &Other)
        {
            DestructElements(GetData(), _ArrayNum);
            MoveOrCopy(*this, Other, _ArrayMax);
        }
        return *this;
    }

    /** Destructor. */
    ~TArrayG()
    {
        DestructElements(GetData(), _ArrayNum);
    }

    /**
     * Helper function for returning a typed pointer to the first array entry.
     *
     * @returns Pointer to first array entry or nullptr if ArrayMax == 0.
     */
    ULANG_FORCEINLINE ElementType* GetData()
    {
        return (ElementType*)_ElementStorage.GetAllocation();
    }

    /**
     * Helper function for returning a typed pointer to the first array entry.
     *
     * @returns Pointer to first array entry or nullptr if ArrayMax == 0.
     */
    ULANG_FORCEINLINE const ElementType* GetData() const
    {
        return (const ElementType*)_ElementStorage.GetAllocation();
    }

    /**
     * Helper function returning the size of the inner type.
     *
     * @returns Size in bytes of array type.
     */
    ULANG_FORCEINLINE uint32_t GetTypeSize() const
    {
        return sizeof(ElementType);
    }

    /**
     * Helper function to return the amount of memory allocated by this
     * container.
     * Only returns the size of allocations made directly by the container, not the elements themselves.
     *
     * @returns Number of bytes allocated by this container.
     */
    ULANG_FORCEINLINE uint32_t GetAllocatedSize(void) const
    {
        return _ElementStorage.GetAllocatedSize(_ArrayMax, sizeof(ElementType));
    }

    /**
     * Returns the amount of slack in this array in elements.
     *
     * @see Num, Shrink
     */
    ULANG_FORCEINLINE int32_t GetSlack() const
    {
        return _ArrayMax - _ArrayNum;
    }

    /**
     * Checks array invariants: if array size is greater than zero and less
     * than maximum.
     */
    ULANG_FORCEINLINE void CheckInvariants() const
    {
        ULANG_ASSERTF((_ArrayNum >= 0) & (_ArrayMax >= _ArrayNum), "Bad array configuration detected."); // & for one branch
    }

    /**
     * Checks if index is in array range.
     *
     * @param Index Index to check.
     */
    ULANG_FORCEINLINE void RangeCheck(int32_t Index) const
    {
        CheckInvariants();

        // Template property, branch will be optimized out
        if (ElementAllocatorType::RequireRangeCheck)
        {
            ULANG_ASSERTF((Index >= 0) & (Index < _ArrayNum), "Array index out of bounds: %i from an array of size %i", Index, _ArrayNum); // & for one branch
        }
    }

    /**
     * Tests if index is valid, i.e. greater than or equal to zero, and less than the number of elements in the array.
     *
     * @param Index Index to test.
     * @returns True if index is valid. False otherwise.
     */
    ULANG_FORCEINLINE bool IsValidIndex(int32_t Index) const
    {
        return Index >= 0 && Index < _ArrayNum;
    }

    /**
     * Returns number of elements in array.
     *
     * @returns Number of elements in array.
     * @see GetSlack
     */
    ULANG_FORCEINLINE int32_t Num() const
    {
        return _ArrayNum;
    }

    /**
     * Returns maximum number of elements in array.
     *
     * @returns Maximum number of elements in array.
     * @see GetSlack
     */
    ULANG_FORCEINLINE int32_t Max() const
    {
        return _ArrayMax;
    }

    /** Accesses the raw allocator. */
    ULANG_FORCEINLINE const typename ElementAllocatorType::RawAllocatorType & GetRawAllocator() const
    {
        return _ElementStorage.GetRawAllocator();
    }

    /**
     * Returns true if no elements in array
     *
     * @returns true if no elements in array or false if one or more elements.
     * @see IsFull, Num
     */
    ULANG_FORCEINLINE bool IsEmpty() const
    {
        return _ArrayNum == 0;
    }

    /**
     * Returns true if any elements in array
     *
     * @returns true if one or more elements in array or false no elements.
     * @see IsEmpty, Num
     */
    ULANG_FORCEINLINE bool IsFilled() const
    {
        return _ArrayNum != 0;
    }

    /**
     * Array bracket operator. Returns reference to element at give index.
     *
     * @returns Reference to indexed element.
     */
    ULANG_FORCEINLINE ElementType& operator[](int32_t Index)
    {
        RangeCheck(Index);
        return GetData()[Index];
    }

    /**
     * Array bracket operator. Returns reference to element at give index.
     *
     * Const version of the above.
     *
     * @returns Reference to indexed element.
     */
    ULANG_FORCEINLINE const ElementType& operator[](int32_t Index) const
    {
        RangeCheck(Index);
        return GetData()[Index];
    }

    /**
     * Pops element from the array.
     *
     * @param bAllowShrinking If this call allows shrinking of the array during element remove.
     * @returns Popped element.
     */
    ULANG_FORCEINLINE ElementType Pop(bool bAllowShrinking = true)
    {
        RangeCheck(0);
        ElementType Result = uLang::MoveIfPossible(GetData()[_ArrayNum - 1]);
        RemoveAt(_ArrayNum - 1, 1, bAllowShrinking);
        return Result;
    }

    /**
     * Pushes element into the array.
     *
     * @param Item Item to push.
     */
    ULANG_FORCEINLINE void Push(ElementType&& Item)
    {
        Add(uLang::MoveIfPossible(Item));
    }

    /**
     * Pushes element into the array.
     *
     * Const ref version of the above.
     *
     * @param Item Item to push.
     * @see Pop, Top
     */
    ULANG_FORCEINLINE void Push(const ElementType& Item)
    {
        Add(Item);
    }

    /**
     * Returns the top element, i.e. the last one.
     *
     * @returns Reference to the top element.
     * @see Pop, Push
     */
    ULANG_FORCEINLINE ElementType& Top()
    {
        return Last();
    }

    /**
     * Returns the top element, i.e. the last one.
     *
     * Const version of the above.
     *
     * @returns Reference to the top element.
     * @see Pop, Push
     */
    ULANG_FORCEINLINE const ElementType& Top() const
    {
        return Last();
    }

    /**
     * Returns n-th last element from the array.
     *
     * @param IndexFromTheEnd (Optional) Index from the end of array (default = 0).
     * @returns Reference to n-th last element from the array.
     */
    ULANG_FORCEINLINE ElementType& Last(int32_t IndexFromTheEnd = 0)
    {
        RangeCheck(_ArrayNum - IndexFromTheEnd - 1);
        return GetData()[_ArrayNum - IndexFromTheEnd - 1];
    }

    /**
     * Returns n-th last element from the array.
     *
     * Const version of the above.
     *
     * @param IndexFromTheEnd (Optional) Index from the end of array (default = 0).
     * @returns Reference to n-th last element from the array.
     */
    ULANG_FORCEINLINE const ElementType& Last(int32_t IndexFromTheEnd = 0) const
    {
        RangeCheck(_ArrayNum - IndexFromTheEnd - 1);
        return GetData()[_ArrayNum - IndexFromTheEnd - 1];
    }

    /**
     * Shrinks the array's used memory to smallest possible to store elements currently in it.
     *
     * @see Slack
     */
    ULANG_FORCEINLINE void Shrink()
    {
        CheckInvariants();
        if (_ArrayMax != _ArrayNum)
        {
            ResizeTo(_ArrayNum);
        }
    }

    /**
     * Finds element within the array.
     *
     * @param Item Item to look for.
     * @param Index Will contain the found index.
     * @returns True if found. False otherwise.
     * @see FindLast, FindLastByPredicate
     */
    ULANG_FORCEINLINE bool Find(const ElementType& Item, int32_t& Index) const
    {
        Index = this->Find(Item);
        return Index != IndexNone;
    }

    /**
     * Finds element within the array.
     *
     * @param Item Item to look for.
     * @returns Index of the found element. IndexNone otherwise.
     * @see FindLast, FindLastByPredicate
     */
    int32_t Find(const ElementType& Item) const
    {
        const ElementType* ULANG_RESTRICT Start = GetData();
        for (const ElementType* ULANG_RESTRICT Data = Start, *ULANG_RESTRICT DataEnd = Data + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (*Data == Item)
            {
                return static_cast<int32_t>(Data - Start);
            }
        }
        return IndexNone;
    }

    /**
     * Finds element within the array starting from the end.
     *
     * @param Item Item to look for.
     * @param Index Output parameter. Found index.
     * @returns True if found. False otherwise.
     * @see Find, FindLastByPredicate
     */
    ULANG_FORCEINLINE bool FindLast(const ElementType& Item, int32_t& Index) const
    {
        Index = this->FindLast(Item);
        return Index != IndexNone;
    }

    /**
     * Finds element within the array starting from the end.
     *
     * @param Item Item to look for.
     * @returns Index of the found element. IndexNone otherwise.
     */
    int32_t FindLast(const ElementType& Item) const
    {
        for (const ElementType* ULANG_RESTRICT Start = GetData(), *ULANG_RESTRICT Data = Start + _ArrayNum; Data != Start; )
        {
            --Data;
            if (*Data == Item)
            {
                return static_cast<int32_t>(Data - Start);
            }
        }
        return IndexNone;
    }

    /**
     * Searches an initial subrange of the array for the last occurrence of an element which matches the specified predicate.
     *
     * @param Pred Predicate taking array element and returns true if element matches search criteria, false otherwise.
     * @param Count The number of elements from the front of the array through which to search.
     * @returns Index of the found element. IndexNone otherwise.
     */
    template <typename Predicate>
    int32_t FindLastByPredicate(Predicate Pred, int32_t Count) const
    {
        ULANG_ASSERTF(Count >= 0 && Count <= this->Num(), "Bad range specified.");
        for (const ElementType* ULANG_RESTRICT Start = GetData(), *ULANG_RESTRICT Data = Start + Count; Data != Start; )
        {
            --Data;
            if (Pred(*Data))
            {
                return static_cast<int32_t>(Data - Start);
            }
        }
        return IndexNone;
    }

    /**
     * Searches the array for the last occurrence of an element which matches the specified predicate.
     *
     * @param Pred Predicate taking array element and returns true if element matches search criteria, false otherwise.
     * @returns Index of the found element. IndexNone otherwise.
     */
    template <typename Predicate>
    ULANG_FORCEINLINE int32_t FindLastByPredicate(Predicate Pred) const
    {
        return FindLastByPredicate(Pred, _ArrayNum);
    }

    /**
     * Finds an item by key (assuming the ElementType overloads operator== for
     * the comparison).
     *
     * @param Key The key to search by.
     * @returns Index to the first matching element, or IndexNone if none is found.
     */
    template <typename KeyType>
    int32_t IndexOfByKey(const KeyType& Key) const
    {
        const ElementType* ULANG_RESTRICT Start = GetData();
        for (const ElementType* ULANG_RESTRICT Data = Start, *ULANG_RESTRICT DataEnd = Start + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (*Data == Key)
            {
                return static_cast<int32_t>(Data - Start);
            }
        }
        return IndexNone;
    }

    /**
     * Finds an item by predicate.
     *
     * @param Pred The predicate to match.
     * @returns Index to the first matching element, or IndexNone if none is found.
     */
    template <typename Predicate>
    int32_t IndexOfByPredicate(Predicate Pred) const
    {
        const ElementType* ULANG_RESTRICT Start = GetData();
        for (const ElementType* ULANG_RESTRICT Data = Start, *ULANG_RESTRICT DataEnd = Start + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (Pred(*Data))
            {
                return static_cast<int32_t>(Data - Start);
            }
        }
        return IndexNone;
    }

    /**
     * Finds an item by key (assuming the ElementType overloads operator== for
     * the comparison).
     *
     * @param Key The key to search by.
     * @returns Pointer to the first matching element, or nullptr if none is found.
     * @see Find
     */
    template <typename KeyType>
    ULANG_FORCEINLINE const ElementType* FindByKey(const KeyType& Key) const
    {
        return const_cast<TArrayG*>(this)->FindByKey(Key);
    }

    /**
     * Finds an item by key (assuming the ElementType overloads operator== for
     * the comparison). Time Complexity: O(n), starts iteration from the beginning so better performance if Key is in the front
     *
     * @param Key The key to search by.
     * @returns Pointer to the first matching element, or nullptr if none is found.
     * @see Find
     */
    template <typename KeyType>
    ElementType* FindByKey(const KeyType& Key)
    {
        for (ElementType* ULANG_RESTRICT Data = GetData(), *ULANG_RESTRICT DataEnd = Data + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (*Data == Key)
            {
                return Data;
            }
        }

        return nullptr;
    }

    /**
     * Finds an element which matches a predicate functor.
     *
     * @param Pred The functor to apply to each element.
     * @returns Pointer to the first element for which the predicate returns true, or nullptr if none is found.
     * @see FilterByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    ULANG_FORCEINLINE const ElementType* FindByPredicate(Predicate Pred) const
    {
        return const_cast<TArrayG*>(this)->FindByPredicate(Pred);
    }

    /**
     * Finds an element which matches a predicate functor.
     *
     * @param Pred The functor to apply to each element. true, or nullptr if none is found.
     * @see FilterByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    ElementType* FindByPredicate(Predicate Pred)
    {
        for (ElementType* ULANG_RESTRICT Data = GetData(), *ULANG_RESTRICT DataEnd = Data + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (Pred(*Data))
            {
                return Data;
            }
        }

        return nullptr;
    }

    /**
     * Filters the elements in the array based on a predicate functor.
     *
     * @param Pred The functor to apply to each element.
     * @returns TArrayG with the same type as this object which contains
     *          the subset of elements for which the functor returns true.
     * @see FindByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    TArrayG FilterByPredicate(Predicate Pred) const
    {
        TArrayG FilterResults;
        for (const ElementType* ULANG_RESTRICT Data = GetData(), *ULANG_RESTRICT DataEnd = Data + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (Pred(*Data))
            {
                FilterResults.Add(*Data);
            }
        }
        return FilterResults;
    }

    /**
     * Checks if this array contains the element.
     *
     * @returns True if found. False otherwise.
     * @see ContainsByPredicate, FilterByPredicate, FindByPredicate
     */
    template <typename ComparisonType>
    bool Contains(const ComparisonType& Item) const
    {
        for (const ElementType* ULANG_RESTRICT Data = GetData(), *ULANG_RESTRICT DataEnd = Data + _ArrayNum; Data != DataEnd; ++Data)
        {
            if (*Data == Item)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * Checks if this array contains element for which the predicate is true.
     *
     * @param Predicate to use
     * @returns True if found. False otherwise.
     * @see Contains, Find
     */
    template <typename Predicate>
    ULANG_FORCEINLINE bool ContainsByPredicate(Predicate Pred) const
    {
        return FindByPredicate(Pred) != nullptr;
    }

    /**
     * Equality operator.
     *
     * @param OtherArray Array to compare.
     * @returns True if this array is the same as OtherArray. False otherwise.
     */
    bool operator==(const TArrayG& OtherArray) const
    {
        int32_t Count = Num();

        return Count == OtherArray.Num() && CompareElements(GetData(), OtherArray.GetData(), Count);
    }

    /**
     * Inequality operator.
     *
     * @param OtherArray Array to compare.
     * @returns True if this array is NOT the same as OtherArray. False otherwise.
     */
    ULANG_FORCEINLINE bool operator!=(const TArrayG& OtherArray) const
    {
        return !(*this == OtherArray);
    }

    /**
     * Adds a given number of uninitialized elements into the array.
     *
     * Caution, AddUninitialized() will create elements without calling
     * the constructor and this is not appropriate for element types that
     * require a constructor to function properly.
     *
     * @param Count Number of elements to add.
     * @returns Number of elements in array before addition.
     */
    ULANG_FORCEINLINE int32_t AddUninitialized(int32_t Count = 1)
    {
        CheckInvariants();
        ULANG_ASSERTF(Count >= 0, "Number of elements to add to array must not be negative.");

        const int32_t OldNum = _ArrayNum;
        if ((_ArrayNum += Count) > _ArrayMax)
        {
            ResizeGrow(OldNum);
        }
        return OldNum;
    }

    /**
     * Inserts a given number of uninitialized elements into the array at given
     * location.
     *
     * Caution, InsertUninitialized() will create elements without calling the
     * constructor and this is not appropriate for element types that require
     * a constructor to function properly.
     *
     * @param Index Tells where to insert the new elements.
     * @param Count Number of elements to add.
     * @see Insert, InsertZeroed, InsertDefaulted
     */
    void InsertUninitialized(int32_t Index, int32_t Count = 1)
    {
        CheckInvariants();
        ULANG_ASSERTF((Count >= 0) & (Index >= 0) & (Index <= _ArrayNum), "Cannot insert elements into array due to invalid parameters.");

        const int32_t OldNum = _ArrayNum;
        if ((_ArrayNum += Count) > _ArrayMax)
        {
            ResizeGrow(OldNum);
        }
        ElementType* Data = GetData() + Index;
        RelocateConstructElements<ElementType>(Data + Count, Data, OldNum - Index);
    }

    /**
     * Inserts a given number of zeroed elements into the array at given
     * location.
     *
     * Caution, InsertZeroed() will create elements without calling the
     * constructor and this is not appropriate for element types that require
     * a constructor to function properly.
     *
     * @param Index Tells where to insert the new elements.
     * @param Count Number of elements to add.
     * @see Insert, InsertUninitialized, InsertDefaulted
     */
    void InsertZeroed(int32_t Index, int32_t Count = 1)
    {
        InsertUninitialized(Index, Count);
        if (Count)
        {
            memset(GetData() + Index, 0, Count * sizeof(ElementType));
        }
    }

    /**
     * Inserts a zeroed element into the array at given location.
     *
     * Caution, InsertZeroed_GetRef() will create an element without calling the
     * constructor and this is not appropriate for element types that require
     * a constructor to function properly.
     *
     * @param Index Tells where to insert the new element.
     * @return A reference to the newly-inserted element.
     * @see Insert_GetRef, InsertDefaulted_GetRef
     */
    ElementType& InsertZeroed_GetRef(int32_t Index)
    {
        InsertUninitialized(Index, 1);
        ElementType* Ptr = GetData() + Index;
        memset(Ptr, 0, sizeof(ElementType));
        return *Ptr;
    }

    /**
     * Inserts a given number of default-constructed elements into the array at a given
     * location.
     *
     * @param Index Tells where to insert the new elements.
     * @param Count Number of elements to add.
     * @see Insert, InsertUninitialized, InsertZeroed
     */
    void InsertDefaulted(int32_t Index, int32_t Count = 1)
    {
        InsertUninitialized(Index, Count);
        DefaultConstructElements<ElementType>(GetData() + Index, Count);
    }

    /**
     * Inserts a default-constructed element into the array at a given
     * location.
     *
     * @param Index Tells where to insert the new element.
     * @return A reference to the newly-inserted element.
     * @see Insert_GetRef, InsertZeroed_GetRef
     */
    ElementType& InsertDefaulted_GetRef(int32_t Index)
    {
        InsertUninitialized(Index, 1);
        ElementType* Ptr = GetData() + Index;
        DefaultConstructElements<ElementType>(Ptr, 1);
        return *Ptr;
    }

    /**
     * Inserts given elements into the array at given location.
     *
     * @param Items Array of elements to insert.
     * @param InIndex Tells where to insert the new elements.
     * @returns Location at which the item was inserted.
     */
    int32_t Insert(std::initializer_list<ElementType> InitList, const int32_t InIndex)
    {
        int32_t NumNewElements = (int32_t)InitList.size();

        InsertUninitialized(InIndex, NumNewElements);
        ConstructElements<ElementType>(GetData() + InIndex, InitList.begin(), NumNewElements);

        return InIndex;
    }

    /**
     * Inserts given elements into the array at given location.
     *
     * @param Items Array of elements to insert.
     * @param InIndex Tells where to insert the new elements.
     * @returns Location at which the item was inserted.
     */
    template <typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    int32_t Insert(const TArrayG<ElementType, OtherElementAllocatorType, OtherRawAllocatorArgsType...>& Items, const int32_t InIndex)
    {
        ULANG_ASSERTF((const void*)this != (const void*)&Items, "Attempted to insert array into itself.");

        int32_t NumNewElements = Items.Num();

        InsertUninitialized(InIndex, NumNewElements);
        ConstructElements<ElementType>(GetData() + InIndex, Items.GetData(), NumNewElements);

        return InIndex;
    }

    /**
     * Inserts given elements into the array at given location.
     *
     * @param Items Array of elements to insert.
     * @param InIndex Tells where to insert the new elements.
     * @returns Location at which the item was inserted.
     */
    template <typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    int32_t Insert(TArrayG<ElementType, OtherElementAllocatorType, OtherRawAllocatorArgsType...>&& Items, const int32_t InIndex)
    {
        ULANG_ASSERTF((const void*)this != (const void*)&Items, "Attempted to insert array into itself.");

        int32_t NumNewElements = Items.Num();

        InsertUninitialized(InIndex, NumNewElements);
        RelocateConstructElements<ElementType>(GetData() + InIndex, Items.GetData(), NumNewElements);
        Items._ArrayNum = 0;

        return InIndex;
    }

    /**
     * Inserts a raw array of elements at a particular index in the TArrayG.
     *
     * @param Ptr A pointer to an array of elements to add.
     * @param Count The number of elements to insert from Ptr.
     * @param Index The index to insert the elements at.
     * @return The index of the first element inserted.
     * @see Add, Remove
     */
    int32_t Insert(const ElementType* Ptr, int32_t Count, int32_t Index)
    {
        ULANG_ASSERTF(Ptr != nullptr, "Elements to insert must not be null.");

        InsertUninitialized(Index, Count);
        ConstructElements<ElementType>(GetData() + Index, Ptr, Count);

        return Index;
    }

    /**
     * Checks that the specified address is not part of an element within the
     * container. Used for implementations to ULANG_ASSERTF that reference arguments
     * aren't going to be invalidated by possible reallocation.
     *
     * @param Addr The address to check.
     * @see Add, Remove
     */
    ULANG_FORCEINLINE void CheckAddress(void const* Addr) const
    {
        ULANG_ASSERTF(Addr < GetData() || Addr >= (GetData() + _ArrayMax), "Attempting to use a container element (%p) which already comes from the container being modified (%p, ArrayMax: %d, ArrayNum: %d, SizeofElement: %d)!", Addr, GetData(), _ArrayMax, _ArrayNum, sizeof(ElementType));
    }

    /**
     * Inserts a given element into the array at given location. Move semantics
     * version.
     *
     * @param Item The element to insert.
     * @param Index Tells where to insert the new elements.
     * @returns Location at which the insert was done.
     * @see Add, Remove
     */
    int32_t Insert(ElementType&& Item, int32_t Index)
    {
        CheckAddress(&Item);

        // construct a copy in place at Index (this new operator will insert at
        // Index, then construct that memory with Item)
        InsertUninitialized(Index, 1);
        new(GetData() + Index) ElementType(uLang::MoveIfPossible(Item));
        return Index;
    }

    /**
     * Inserts a given element into the array at given location.
     *
     * @param Item The element to insert.
     * @param Index Tells where to insert the new elements.
     * @returns Location at which the insert was done.
     * @see Add, Remove
     */
    int32_t Insert(const ElementType& Item, int32_t Index)
    {
        CheckAddress(&Item);

        // construct a copy in place at Index (this new operator will insert at
        // Index, then construct that memory with Item)
        InsertUninitialized(Index, 1);
        new(GetData() + Index) ElementType(Item);
        return Index;
    }

    /**
     * Inserts a given element into the array at given location. Move semantics
     * version.
     *
     * @param Item The element to insert.
     * @param Index Tells where to insert the new element.
     * @return A reference to the newly-inserted element.
     * @see Add, Remove
     */
    ElementType& Insert_GetRef(ElementType&& Item, int32_t Index)
    {
        CheckAddress(&Item);

        // construct a copy in place at Index (this new operator will insert at
        // Index, then construct that memory with Item)
        InsertUninitialized(Index, 1);
        ElementType* Ptr = GetData() + Index;
        new(Ptr) ElementType(uLang::MoveIfPossible(Item));
        return *Ptr;
    }

    /**
     * Inserts a given element into the array at given location.
     *
     * @param Item The element to insert.
     * @param Index Tells where to insert the new element.
     * @return A reference to the newly-inserted element.
     * @see Add, Remove
     */
    ElementType& Insert_GetRef(const ElementType& Item, int32_t Index)
    {
        CheckAddress(&Item);

        // construct a copy in place at Index (this new operator will insert at
        // Index, then construct that memory with Item)
        InsertUninitialized(Index, 1);
        ElementType* Ptr = GetData() + Index;
        new(Ptr) ElementType(Item);
        return *Ptr;
    }

private:
    void RemoveAtImpl(int32_t Index, int32_t Count, bool bAllowShrinking)
    {
        if (Count)
        {
            CheckInvariants();
            ULANG_ASSERTF((Count >= 0) & (Index >= 0) & (Index + Count <= _ArrayNum), "Cannot remove elements from array due to invalid parameters.");

            DestructElements(GetData() + Index, Count);

            // Skip memmove in the common case that there is nothing to move.
            int32_t NumToMove = _ArrayNum - Index - Count;
            if (NumToMove)
            {
                memmove
                    (
                    (uint8_t*)_ElementStorage.GetAllocation() + (Index)* sizeof(ElementType),
                    (uint8_t*)_ElementStorage.GetAllocation() + (Index + Count) * sizeof(ElementType),
                    NumToMove * sizeof(ElementType)
                    );
            }
            _ArrayNum -= Count;

            if (bAllowShrinking)
            {
                ResizeShrink();
            }
        }
    }

public:
    /**
     * Removes an element (or elements) at given location optionally shrinking
     * the array.
     *
     * @param Index Location in array of the element to remove.
     * @param Count (Optional) Number of elements to remove. Default is 1.
     * @param bAllowShrinking (Optional) Tells if this call can shrink array if suitable after remove. Default is true.
     */
    ULANG_FORCEINLINE void RemoveAt(int32_t Index)
    {
        RemoveAtImpl(Index, 1, true);
    }

    /**
     * Removes an element (or elements) at given location optionally shrinking
     * the array.
     *
     * @param Index Location in array of the element to remove.
     * @param Count (Optional) Number of elements to remove. Default is 1.
     * @param bAllowShrinking (Optional) Tells if this call can shrink array if suitable after remove. Default is true.
     */
    template <typename CountType>
    ULANG_FORCEINLINE void RemoveAt(int32_t Index, CountType Count, bool bAllowShrinking = true)
    {
        static_assert(!TAreTypesEqual<CountType, bool>::Value, "TArrayG::RemoveAt: unexpected bool passed as the Count argument");
        RemoveAtImpl(Index, Count, bAllowShrinking);
    }

private:
    void RemoveAtSwapImpl(int32_t Index, int32_t Count = 1, bool bAllowShrinking = true)
    {
        if (Count)
        {
            CheckInvariants();
            ULANG_ASSERTF((Count >= 0) & (Index >= 0) & (Index + Count <= _ArrayNum), "Cannot remove elements from array due to invalid parameters.");

            DestructElements(GetData() + Index, Count);

            // Replace the elements in the hole created by the removal with elements from the end of the array, so the range of indices used by the array is contiguous.
            const int32_t NumElementsInHole = Count;
            const int32_t NumElementsAfterHole = _ArrayNum - (Index + Count);
            const int32_t NumElementsToMoveIntoHole = CMath::Min(NumElementsInHole, NumElementsAfterHole);
            if (NumElementsToMoveIntoHole)
            {
                memcpy(
                    (uint8_t*)_ElementStorage.GetAllocation() + (Index)* sizeof(ElementType),
                    (uint8_t*)_ElementStorage.GetAllocation() + (_ArrayNum - NumElementsToMoveIntoHole) * sizeof(ElementType),
                    NumElementsToMoveIntoHole * sizeof(ElementType)
                    );
            }
            _ArrayNum -= Count;

            if (bAllowShrinking)
            {
                ResizeShrink();
            }
        }
    }

public:
    /**
     * Removes an element (or elements) at given location optionally shrinking
     * the array.
     *
     * This version is much more efficient than RemoveAt (O(Count) instead of
     * O(ArrayNum)), but does not preserve the order.
     *
     * @param Index Location in array of the element to remove.
     * @param Count (Optional) Number of elements to remove. Default is 1.
     * @param bAllowShrinking (Optional) Tells if this call can shrink array if
     *                        suitable after remove. Default is true.
     */
    ULANG_FORCEINLINE void RemoveAtSwap(int32_t Index)
    {
        RemoveAtSwapImpl(Index, 1, true);
    }

    /**
     * Removes an element (or elements) at given location optionally shrinking
     * the array.
     *
     * This version is much more efficient than RemoveAt (O(Count) instead of
     * O(ArrayNum)), but does not preserve the order.
     *
     * @param Index Location in array of the element to remove.
     * @param Count (Optional) Number of elements to remove. Default is 1.
     * @param bAllowShrinking (Optional) Tells if this call can shrink array if
     *                        suitable after remove. Default is true.
     */
    template <typename CountType>
    ULANG_FORCEINLINE void RemoveAtSwap(int32_t Index, CountType Count, bool bAllowShrinking = true)
    {
        static_assert(!TAreTypesEqual<CountType, bool>::Value, "TArrayG::RemoveAtSwap: unexpected bool passed as the Count argument");
        RemoveAtSwapImpl(Index, Count, bAllowShrinking);
    }

    /**
     * Replaces specified element with the supplied element.
     *
     * @param OldItem The element to replace.
     * @param NewItem The element to replace with.
     * @returns Location at which the replace was done.
     * @see Add, Remove, Insert
     */
    int32_t Replace(const ElementType& OldItem, const ElementType& NewItem)
    {
        int32_t Index = Find(OldItem);

        if (Index != IndexNone)
        {
            CheckAddress(&NewItem);
            GetData()[Index] = NewItem;
        }

        return Index;
    }

    /**
     * Same as empty, but doesn't change memory allocations, unless the new size is larger than
     * the current array. It calls the destructors on held items if needed and then zeros the ArrayNum.
     *
     * @param NewSize The expected usage size after calling this function.
     */
    void Reset(int32_t NewSize = 0)
    {
        // If we have space to hold the excepted size, then don't reallocate
        if (NewSize <= _ArrayMax)
        {
            DestructElements(GetData(), _ArrayNum);
            _ArrayNum = 0;
        }
        else
        {
            Empty(NewSize);
        }
    }

    /**
     * Empties the array. It calls the destructors on held items if needed.
     *
     * @param Slack (Optional) The expected usage size after empty operation. Default is 0.
     */
    void Empty(int32_t Slack = 0)
    {
        DestructElements(GetData(), _ArrayNum);

        ULANG_ASSERTF(Slack >= 0, "Array slack must be positive.");
        _ArrayNum = 0;

        if (_ArrayMax != Slack)
        {
            ResizeTo(Slack);
        }
    }

    /**
     * Resizes array to given number of elements.
     *
     * @param NewNum New size of the array.
     * @param bAllowShrinking Tell if this function can shrink the memory in-use if suitable.
     */
    void SetNum(int32_t NewNum, bool bAllowShrinking = true)
    {
        if (NewNum > Num())
        {
            const int32_t Diff = NewNum - _ArrayNum;
            const int32_t Index = AddUninitialized(Diff);
            DefaultConstructElements<ElementType>((uint8_t*)_ElementStorage.GetAllocation() + Index * sizeof(ElementType), Diff);
        }
        else if (NewNum < Num())
        {
            RemoveAt(NewNum, Num() - NewNum, bAllowShrinking);
        }
    }

    /**
     * Resizes array to given number of elements. New elements will be zeroed.
     *
     * @param NewNum New size of the array.
     */
    void SetNumZeroed(int32_t NewNum, bool bAllowShrinking = true)
    {
        if (NewNum > Num())
        {
            AddZeroed(NewNum - Num());
        }
        else if (NewNum < Num())
        {
            RemoveAt(NewNum, Num() - NewNum, bAllowShrinking);
        }
    }

    /**
     * Resizes array to given number of elements. New elements will be uninitialized.
     *
     * @param NewNum New size of the array.
     */
    void SetNumUninitialized(int32_t NewNum, bool bAllowShrinking = true)
    {
        if (NewNum > Num())
        {
            AddUninitialized(NewNum - Num());
        }
        else if (NewNum < Num())
        {
            RemoveAt(NewNum, Num() - NewNum, bAllowShrinking);
        }
    }

    /**
     * Does nothing except setting the new number of elements in the array. Does not destruct items, does not de-allocate memory.
     * @param NewNum New number of elements in the array, must be <= the current number of elements in the array.
     */
    void SetNumUnsafeInternal(int32_t NewNum)
    {
        ULANG_ASSERTF(NewNum <= Num() && NewNum >= 0, "Incorrect new array size.");
        _ArrayNum = NewNum;
    }

    /**
     * Appends the specified array to this array.
     *
     * Allocator changing version.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    template <typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    void Append(const TArrayG<ElementType, OtherElementAllocatorType, OtherRawAllocatorArgsType...>& Source)
    {
        ULANG_ASSERTF((void*)this != (void*)&Source, "Attempted to append array to itself.");

        int32_t SourceCount = Source.Num();

        // Do nothing if the source is empty.
        if (!SourceCount)
        {
            return;
        }

        // Allocate memory for the new elements.
        Reserve(_ArrayNum + SourceCount);
        ConstructElements<ElementType>(GetData() + _ArrayNum, Source.GetData(), SourceCount);

        _ArrayNum += SourceCount;
    }

    /**
     * Appends the specified array to this array.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    template <typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    void Append(TArrayG<ElementType, OtherElementAllocatorType, OtherRawAllocatorArgsType...>&& Source)
    {
        ULANG_ASSERTF((void*)this != (void*)&Source, "Attempted to append array to itself.");

        int32_t SourceCount = Source.Num();

        // Do nothing if the source is empty.
        if (!SourceCount)
        {
            return;
        }

        // Allocate memory for the new elements.
        Reserve(_ArrayNum + SourceCount);
        RelocateConstructElements<ElementType>(GetData() + _ArrayNum, Source.GetData(), SourceCount);
        Source._ArrayNum = 0;

        _ArrayNum += SourceCount;
    }

    /**
     * Adds a raw array of elements to the end of the TArrayG.
     *
     * @param Ptr   A pointer to an array of elements to add.
     * @param Count The number of elements to insert from Ptr.
     * @see Add, Insert
     */
    void Append(const ElementType* Ptr, int32_t Count)
    {
        ULANG_ASSERTF(Ptr != nullptr || Count == 0, "Attempted to append invalid array.");

        int32_t Pos = AddUninitialized(Count);
        ConstructElements<ElementType>(GetData() + Pos, Ptr, Count);
    }

    /**
     * Adds an initializer list of elements to the end of the TArrayG.
     *
     * @param InitList The initializer list of elements to add.
     * @see Add, Insert
     */
    ULANG_FORCEINLINE void Append(std::initializer_list<ElementType> InitList)
    {
        int32_t Count = (int32_t)InitList.size();

        int32_t Pos = AddUninitialized(Count);
        ConstructElements<ElementType>(GetData() + Pos, InitList.begin(), Count);
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * Move semantics version.
     *
     * @param Other The array to append.
     */
    TArrayG& operator+=(TArrayG&& Other)
    {
        Append(Move(Other));
        return *this;
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * @param Other The array to append.
     */
    TArrayG& operator+=(const TArrayG& Other)
    {
        Append(Other);
        return *this;
    }

    /**
     * Appends the specified initializer list to this array.
     *
     * @param InitList The initializer list to append.
     */
    TArrayG& operator+=(std::initializer_list<ElementType> InitList)
    {
        Append(InitList);
        return *this;
    }

    /**
     * Constructs a new item at the end of the array, possibly reallocating the whole array to fit.
     *
     * @param Args  The arguments to forward to the constructor of the new item.
     * @return      Index to the new item
     */
    template <typename... ArgsType>
    ULANG_FORCEINLINE int32_t Emplace(ArgsType&&... Args)
    {
        const int32_t Index = AddUninitialized(1);
        new(GetData() + Index) ElementType(ForwardArg<ArgsType>(Args)...);
        return Index;
    }

    /**
     * Constructs a new item at the end of the array, possibly reallocating the whole array to fit.
     *
     * @param Args  The arguments to forward to the constructor of the new item.
     * @return A reference to the newly-inserted element.
     */
    template <typename... ArgsType>
    ULANG_FORCEINLINE ElementType& Emplace_GetRef(ArgsType&&... Args)
    {
        const int32_t Index = AddUninitialized(1);
        ElementType* Ptr = GetData() + Index;
        new(Ptr) ElementType(ForwardArg<ArgsType>(Args)...);
        return *Ptr;
    }

    /**
     * Constructs a new item at a specified index, possibly reallocating the whole array to fit.
     *
     * @param Index The index to add the item at.
     * @param Args  The arguments to forward to the constructor of the new item.
     */
    template <typename... ArgsType>
    ULANG_FORCEINLINE void EmplaceAt(int32_t Index, ArgsType&&... Args)
    {
        InsertUninitialized(Index, 1);
        new(GetData() + Index) ElementType(ForwardArg<ArgsType>(Args)...);
    }

    /**
     * Constructs a new item at a specified index, possibly reallocating the whole array to fit.
     *
     * @param Index The index to add the item at.
     * @param Args  The arguments to forward to the constructor of the new item.
     * @return A reference to the newly-inserted element.
     */
    template <typename... ArgsType>
    ULANG_FORCEINLINE ElementType& EmplaceAt_GetRef(int32_t Index, ArgsType&&... Args)
    {
        InsertUninitialized(Index, 1);
        ElementType* Ptr = GetData() + Index;
        new(Ptr) ElementType(ForwardArg<ArgsType>(Args)...);
        return *Ptr;
    }

    /**
     * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
     *
     * Move semantics version.
     *
     * @param Item The item to add
     * @return Index to the new item
     * @see AddDefaulted, AddUnique, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t Add(ElementType&& Item)
    {
        CheckAddress(&Item);
        return Emplace(uLang::MoveIfPossible(Item));
    }

    /**
     * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
     *
     * @param Item The item to add
     * @return Index to the new item
     * @see AddDefaulted, AddUnique, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t Add(const ElementType& Item)
    {
        CheckAddress(&Item);
        return Emplace(Item);
    }

    /**
     * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
     *
     * Move semantics version.
     *
     * @param Item The item to add
     * @return A reference to the newly-inserted element.
     * @see AddDefaulted_GetRef, AddUnique_GetRef, AddZeroed_GetRef, Insert_GetRef
     */
    ULANG_FORCEINLINE ElementType& Add_GetRef(ElementType&& Item)
    {
        CheckAddress(&Item);
        return Emplace_GetRef(uLang::MoveIfPossible(Item));
    }

    /**
     * Adds a new item to the end of the array, possibly reallocating the whole array to fit.
     *
     * @param Item The item to add
     * @return A reference to the newly-inserted element.
     * @see AddDefaulted_GetRef, AddUnique_GetRef, AddZeroed_GetRef, Insert_GetRef
     */
    ULANG_FORCEINLINE ElementType& Add_GetRef(const ElementType& Item)
    {
        CheckAddress(&Item);
        return Emplace_GetRef(Item);
    }

    /**
     * Adds new items to the end of the array, possibly reallocating the whole
     * array to fit. The new items will be zeroed.
     *
     * Caution, AddZeroed() will create elements without calling the
     * constructor and this is not appropriate for element types that require
     * a constructor to function properly.
     *
     * @param  Count  The number of new items to add.
     * @return Index to the first of the new items.
     * @see Add, AddDefaulted, AddUnique, Append, Insert
     */
    int32_t AddZeroed(int32_t Count = 1)
    {
        const int32_t Index = AddUninitialized(Count);
        if (Count)
        {
            memset((uint8_t*)_ElementStorage.GetAllocation() + Index*sizeof(ElementType), 0, Count*sizeof(ElementType));
        }
        return Index;
    }

    /**
     * Adds a new item to the end of the array, possibly reallocating the whole
     * array to fit. The new item will be zeroed.
     *
     * Caution, AddZeroed_GetRef() will create elements without calling the
     * constructor and this is not appropriate for element types that require
     * a constructor to function properly.
     *
     * @return A reference to the newly-inserted element.
     * @see Add_GetRef, AddDefaulted_GetRef, AddUnique_GetRef, Insert_GetRef
     */
    ElementType& AddZeroed_GetRef()
    {
        const int32_t Index = AddUninitialized(1);
        ElementType* Ptr = GetData() + Index;
        memset(Ptr, 0, sizeof(ElementType));
        return *Ptr;
    }

    /**
     * Adds new items to the end of the array, possibly reallocating the whole
     * array to fit. The new items will be default-constructed.
     *
     * @param  Count  The number of new items to add.
     * @return Index to the first of the new items.
     * @see Add, AddZeroed, AddUnique, Append, Insert
     */
    int32_t AddDefaulted(int32_t Count = 1)
    {
        const int32_t Index = AddUninitialized(Count);
        DefaultConstructElements<ElementType>((uint8_t*)_ElementStorage.GetAllocation() + Index * sizeof(ElementType), Count);
        return Index;
    }

    /**
     * Add a new item to the end of the array, possibly reallocating the whole
     * array to fit. The new item will be default-constructed.
     *
     * @return A reference to the newly-inserted element.
     * @see Add_GetRef, AddZeroed_GetRef, AddUnique_GetRef, Insert_GetRef
     */
    ElementType& AddDefaulted_GetRef()
    {
        const int32_t Index = AddUninitialized(1);
        ElementType* Ptr = GetData() + Index;
        DefaultConstructElements<ElementType>(Ptr, 1);
        return *Ptr;
    }

private:

    /**
     * Adds unique element to array if it doesn't exist.
     *
     * @param Args Item to add.
     * @returns Index of the element in the array.
     */
    template <typename ArgsType>
    int32_t AddUniqueImpl(ArgsType&& Args)
    {
        int32_t Index;
        if (Find(Args, Index))
        {
            return Index;
        }

        return Add(ForwardArg<ArgsType>(Args));
    }

public:

    /**
     * Adds unique element to array if it doesn't exist.
     *
     * Move semantics version.
     *
     * @param Args Item to add.
     * @returns Index of the element in the array.
     * @see Add, AddDefaulted, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t AddUnique(ElementType&& Item) { return AddUniqueImpl(uLang::MoveIfPossible(Item)); }

    /**
     * Adds unique element to array if it doesn't exist.
     *
     * @param Args Item to add.
     * @returns Index of the element in the array.
     * @see Add, AddDefaulted, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t AddUnique(const ElementType& Item) { return AddUniqueImpl(Item); }

    /**
     * Reserves memory such that the array can contain at least Number elements.
     *
     * @param Number The number of elements that the array should be able to contain after allocation.
     * @see Shrink
     */
    ULANG_FORCEINLINE void Reserve(int32_t Number)
    {
        if (Number > _ArrayMax)
        {
            ResizeTo(Number);
        }
    }

    /**
     * Sets the size of the array, filling it with the given element.
     *
     * @param Element The element to fill array with.
     * @param Number The number of elements that the array should be able to contain after allocation.
     */
    void Init(const ElementType& Element, int32_t Number)
    {
        Empty(Number);
        for (int32_t Index = 0; Index < Number; ++Index)
        {
            new(*this) ElementType(Element);
        }
    }

    /**
     * Removes the first occurrence of the specified item in the array,
     * maintaining order but not indices.
     *
     * @param Item The item to remove.
     * @returns The number of items removed. For RemoveSingleItem, this is always either 0 or 1.
     * @see Add, Insert, Remove, RemoveAll, RemoveAllSwap
     */
    int32_t RemoveSingle(const ElementType& Item)
    {
        int32_t Index = Find(Item);
        if (Index == IndexNone)
        {
            return 0;
        }

        auto* RemovePtr = GetData() + Index;

        // Destruct items that match the specified Item.
        DestructElements(RemovePtr, 1);
        RelocateConstructElements<ElementType>(RemovePtr, RemovePtr + 1, _ArrayNum - (Index + 1));

        // Update the array count
        --_ArrayNum;

        // Removed one item
        return 1;
    }

    /**
     * Removes as many instances of Item as there are in the array, maintaining
     * order but not indices.
     *
     * @param Item Item to remove from array.
     * @returns Number of removed elements.
     * @see Add, Insert, RemoveAll, RemoveAllSwap, RemoveSingle, RemoveSwap
     */
    template <typename OtherElementType>
    int32_t Remove(const OtherElementType& Item)
    {
        CheckAddress(&Item);

        // Element is non-const to preserve compatibility with existing code with a non-const operator==() member function
        return RemoveAll([&Item](ElementType& Element) { return Element == Item; });
    }

    /**
     * Remove all instances that match the predicate, maintaining order but not indices
     * Optimized to work with runs of matches/non-matches
     *
     * @param Predicate Predicate class instance
     * @returns Number of removed elements.
     * @see Add, Insert, RemoveAllSwap, RemoveSingle, RemoveSwap
     */
    template <class PREDICATE_CLASS>
    int32_t RemoveAll(const PREDICATE_CLASS& Predicate)
    {
        const int32_t OriginalNum = _ArrayNum;
        if (!OriginalNum)
        {
            return 0; // nothing to do, loop assumes one item so need to deal with this edge case here
        }

        int32_t WriteIndex = 0;
        int32_t ReadIndex = 0;
        bool NotMatch = !Predicate(GetData()[ReadIndex]); // use a ! to guarantee it can't be anything other than zero or one
        do
        {
            int32_t RunStartIndex = ReadIndex++;
            while (ReadIndex < OriginalNum && NotMatch == !Predicate(GetData()[ReadIndex]))
            {
                ReadIndex++;
            }
            int32_t RunLength = ReadIndex - RunStartIndex;
            ULANG_ASSERTF(RunLength > 0, "RunLength must be positive here.");
            if (NotMatch)
            {
                // this was a non-matching run, we need to move it
                if (WriteIndex != RunStartIndex)
                {
                    RelocateConstructElements<ElementType>(&GetData()[WriteIndex], &GetData()[RunStartIndex], RunLength);
                }
                WriteIndex += RunLength;
            }
            else
            {
                // this was a matching run, delete it
                DestructElements(GetData() + RunStartIndex, RunLength);
            }
            NotMatch = !NotMatch;
        } while (ReadIndex < OriginalNum);

        _ArrayNum = WriteIndex;
        return OriginalNum - _ArrayNum;
    }

    /**
     * Remove all instances that match the predicate
     *
     * @param Predicate Predicate class instance
     * @see Remove, RemoveSingle, RemoveSingleSwap, RemoveSwap
     */
    template <class PREDICATE_CLASS>
    void RemoveAllSwap(const PREDICATE_CLASS& Predicate, bool bAllowShrinking = true)
    {
        for (int32_t ItemIndex = 0; ItemIndex < Num();)
        {
            if (Predicate((*this)[ItemIndex]))
            {
                RemoveAtSwap(ItemIndex, 1, bAllowShrinking);
            }
            else
            {
                ++ItemIndex;
            }
        }
    }

    /**
     * Removes the first occurrence of the specified item in the array. This version is much more efficient
     * O(Count) instead of O(ArrayNum), but does not preserve the order
     *
     * @param Item The item to remove
     *
     * @returns The number of items removed. For RemoveSingleItem, this is always either 0 or 1.
     * @see Add, Insert, Remove, RemoveAll, RemoveAllSwap, RemoveSwap
     */
    int32_t RemoveSingleSwap(const ElementType& Item, bool bAllowShrinking = true)
    {
        int32_t Index = Find(Item);
        if (Index == IndexNone)
        {
            return 0;
        }

        RemoveAtSwap(Index, 1, bAllowShrinking);

        // Removed one item
        return 1;
    }

    /**
     * Removes item from the array.
     *
     * This version is much more efficient, because it uses RemoveAtSwap
     * internally which is O(Count) instead of RemoveAt which is O(ArrayNum),
     * but does not preserve the order.
     *
     * @returns Number of elements removed.
     * @see Add, Insert, Remove, RemoveAll, RemoveAllSwap
     */
    int32_t RemoveSwap(const ElementType& Item)
    {
        CheckAddress(&Item);

        const int32_t OriginalNum = _ArrayNum;
        for (int32_t Index = 0; Index < _ArrayNum; Index++)
        {
            if ((*this)[Index] == Item)
            {
                RemoveAtSwap(Index--);
            }
        }
        return OriginalNum - _ArrayNum;
    }

public:

    /**
     * DO NOT USE DIRECTLY
     * STL-like iterators to enable range-based for loop support.
     */
    ULANG_FORCEINLINE ElementType *       begin()       { return GetData(); }
    ULANG_FORCEINLINE const ElementType * begin() const { return GetData(); }
    ULANG_FORCEINLINE ElementType *       end()         { return GetData() + Num(); }
    ULANG_FORCEINLINE const ElementType * end() const   { return GetData() + Num(); }

public:

    /**
     * Sorts the array assuming < operator is defined for the item type.
     *
     * @note: If your array contains raw pointers, they will be automatically dereferenced during sorting.
     *        Therefore, your array will be sorted by the values being pointed to, rather than the pointers' values.
     *        If this is not desirable, please use Algo::Sort(MyArray) instead.
     *        The auto-dereferencing behavior does not occur with smart pointers.
     */
    void Sort()
    {
        Algo::Sort( *this, TDereferenceWrapper<TLess<ElementType>>( TLess<ElementType>() ) );
    }

    /**
     * Sorts the array using user define predicate class.
     *
     * @param Predicate Predicate instance or lambda.
     *
     * @note: If your array contains raw pointers, they will be automatically dereferenced during sorting.
     *        Therefore, your predicate will be passed references rather than pointers.
     *        If this is not desirable, please use Algo::Sort(MyArray, Predicate) instead.
     *        The auto-dereferencing behavior does not occur with smart pointers.
     */
    template <class PredicateType>
    void Sort(const PredicateType& Predicate)
    {
        Algo::Sort( *this, TDereferenceWrapper<PredicateType>( Predicate ) );
    }

    /**
     * Remove successive duplicates. Assumes that elements of the array have ==.
     */
    void RemoveSuccessiveDuplicates()
    {
        ElementType* LastEntry = nullptr;
        int32_t ToIndex = 0;
        int32_t FromIndex = 0;
        while (FromIndex < Num())
        {
            ElementType ThisEntry = Move((*this)[FromIndex++]);
            if (!LastEntry || !(*LastEntry == ThisEntry))
            {
                LastEntry = &(*this)[ToIndex];
                (*this)[ToIndex++] = Move(ThisEntry);
            }
        }
        RemoveAt(ToIndex, Num() - ToIndex);
    }

private:

    ULANG_FORCENOINLINE void ResizeGrow(int32_t OldNum)
    {
        _ArrayMax = _ElementStorage.CalculateSlackGrow(_ArrayNum, _ArrayMax, sizeof(ElementType));
        _ElementStorage.ResizeAllocation(OldNum, _ArrayMax, sizeof(ElementType));
    }
    ULANG_FORCENOINLINE void ResizeShrink()
    {
        const int32_t NewArrayMax = _ElementStorage.CalculateSlackShrink(_ArrayNum, _ArrayMax, sizeof(ElementType));
        if (NewArrayMax != _ArrayMax)
        {
            _ArrayMax = NewArrayMax;
            ULANG_ASSERTF(_ArrayMax >= _ArrayNum, "Attempted to shrink array to less than its count.");
            _ElementStorage.ResizeAllocation(_ArrayNum, _ArrayMax, sizeof(ElementType));
        }
    }
    ULANG_FORCENOINLINE void ResizeTo(int32_t NewMax)
    {
        if (NewMax)
        {
            NewMax = _ElementStorage.CalculateSlackReserve(NewMax, sizeof(ElementType));
        }
        if (NewMax != _ArrayMax)
        {
            _ArrayMax = NewMax;
            _ElementStorage.ResizeAllocation(_ArrayNum, _ArrayMax, sizeof(ElementType));
        }
    }
    ULANG_FORCENOINLINE void ResizeForCopy(int32_t NewMax, int32_t PrevMax)
    {
        if (NewMax)
        {
            NewMax = _ElementStorage.CalculateSlackReserve(NewMax, sizeof(ElementType));
        }
        if (NewMax != PrevMax)
        {
            _ElementStorage.ResizeAllocation(0, NewMax, sizeof(ElementType));
        }
        _ArrayMax = NewMax;
    }


    /**
     * Copies data from one array into this array. Uses the fast path if the
     * data in question does not need a constructor.
     *
     * @param Source The source array to copy
     * @param PrevMax The previous allocated size
     * @param ExtraSlack Additional amount of memory to allocate at
     *                   the end of the buffer. Counted in elements. Zero by
     *                   default.
     */
    template <typename OtherElementType>
    void CopyToEmpty(const OtherElementType* OtherData, int32_t OtherNum, int32_t PrevMax, int32_t ExtraSlack)
    {
        ULANG_ASSERTF(ExtraSlack >= 0, "Array slack must be positive.");
        _ArrayNum = OtherNum;
        if (OtherNum || ExtraSlack || PrevMax)
        {
            ResizeForCopy(OtherNum + ExtraSlack, PrevMax);
            ConstructElements<ElementType>(GetData(), OtherData, OtherNum);
        }
        else
        {
            _ArrayMax = 0;
        }
    }

protected:

    using ElementStorageType = typename TChooseClass<
        ElementAllocatorType::NeedsElementType,
        typename ElementAllocatorType::template ForElementType<ElementType>,
        typename ElementAllocatorType::ForAnyElementType
        >::Result;

    ElementStorageType _ElementStorage;
    int32_t            _ArrayNum;
    int32_t            _ArrayMax;

};

/// Array that allocates elements on the heap
template<class ElementType>
using TArray = TArrayG<ElementType, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Array that allocates object using a given allocator instance
template<class ElementType>
using TArrayA = TArrayG<ElementType, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;

template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsZeroConstructType<TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = TAllocatorTraits<ElementAllocatorType>::IsZeroConstruct };
};

template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TContainerTraits<TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>> : public TContainerTraitsBase<TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    static_assert(TAllocatorTraits<ElementAllocatorType>::SupportsMove, "TArrayG no longer supports move-unaware allocators");
    enum { MoveWillEmptyContainer = TAllocatorTraits<ElementAllocatorType>::SupportsMove };
};

template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsContiguousContainer<TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = true };
};


/**
 * Traits class which determines whether or not a type is a TArray.
 */
template <typename T> struct TIsTArray { enum { Value = false }; };

template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType> struct TIsTArray<               TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>> { enum { Value = true }; };
template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType> struct TIsTArray<const          TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>> { enum { Value = true }; };
template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType> struct TIsTArray<      volatile TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>> { enum { Value = true }; };
template <typename ElementType, typename ElementAllocatorType, typename... RawAllocatorArgsType> struct TIsTArray<const volatile TArrayG<ElementType, ElementAllocatorType, RawAllocatorArgsType...>> { enum { Value = true }; };

template <typename T>
ULANG_FORCEINLINE uint32_t GetTypeHash(const TArray<T> Array)
{
    uint32_t Result = 0;
    for (const T& Element : Array)
    {
        Result = HashCombineFast(Result, GetTypeHash(Element));
    }
    return Result;
}
}
