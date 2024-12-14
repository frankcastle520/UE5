// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/UniquePointer.h"

namespace uLang
{

/**
 * Templated dynamic array of unique pointers to elements
 **/
template<typename InElementType, bool AllowNull, typename InElementAllocatorType, typename... RawAllocatorArgsType>
class TUPtrArrayG
{
    template <typename OtherElementType, bool OtherAllowNull, typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    friend class TUPtrArrayG;

public:

    using ElementType = InElementType;
    using ElementAllocatorType = InElementAllocatorType;
    using PointerType = TUPtrG<ElementType, AllowNull, typename InElementAllocatorType::RawAllocatorType, RawAllocatorArgsType...>;
    using PointerStorageType = TArrayG<ElementType *, InElementAllocatorType, RawAllocatorArgsType...>;

    /**
     * Constructor
     */
    ULANG_FORCEINLINE TUPtrArrayG(RawAllocatorArgsType&&... RawAllocatorArgs)
        : _PointerStorage(uLang::ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
    {}

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     */
    ULANG_FORCEINLINE TUPtrArrayG(TUPtrArrayG && Other)
        : _PointerStorage(ForwardArg<PointerStorageType>(Other._PointerStorage))
    {
    }

    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     * @param ExtraSlack Tells how much extra pointer memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    TUPtrArrayG(TUPtrArrayG && Other, int32_t ExtraSlack)
        : _PointerStorage(ForwardArg<PointerStorageType>(Other._PointerStorage), ExtraSlack)
    {
    }

    /**
     * Move assignment operator.
     *
     * @param Other Array to assign and move from.
     */
    TUPtrArrayG& operator=(TUPtrArrayG&& Other)
    {
        if (this != &Other)
        {
            DeleteAll();
            _PointerStorage = ForwardArg<PointerStorageType>(Other._PointerStorage);
        }
        return *this;
    }

    /** Destructor. */
    ~TUPtrArrayG()
    {
        DeleteAll();
    }

    /**
     * Returns the amount of slack in this array in elements.
     *
     * @see Num, Shrink
     */
    ULANG_FORCEINLINE int32_t GetSlack() const
    {
        return _PointerStorage.GetSlack();
    }

    /**
     * Tests if index is valid, i.e. greater than or equal to zero, and less than the number of elements in the array.
     *
     * @param Index Index to test.
     * @returns True if index is valid. False otherwise.
     */
    ULANG_FORCEINLINE bool IsValidIndex(int32_t Index) const
    {
        return _PointerStorage.IsValidIndex(Index);
    }

    /**
     * Returns number of elements in array.
     *
     * @returns Number of elements in array.
     * @see GetSlack
     */
    ULANG_FORCEINLINE int32_t Num() const
    {
        return _PointerStorage.Num();
    }

    /**
     * Returns maximum number of elements in array.
     *
     * @returns Maximum number of elements in array.
     * @see GetSlack
     */
    ULANG_FORCEINLINE int32_t Max() const
    {
        return _PointerStorage.Max();
    }

    /**
     * Returns true if no elements in array
     *
     * @returns true if no elements in array or false if one or more elements.
     * @see IsFull, Num
     */
    ULANG_FORCEINLINE bool IsEmpty() const
    {
        return _PointerStorage.IsEmpty();
    }

    /**
     * Returns true if any elements in array
     *
     * @returns true if one or more elements in array or false no elements.
     * @see IsEmpty, Num
     */
    ULANG_FORCEINLINE bool IsFilled() const
    {
        return _PointerStorage.IsFilled();
    }

    /**
     * Array bracket operator. Returns naked pointer to element at given index.
     */
    ULANG_FORCEINLINE ElementType* operator[](int32_t Index) const
    {
        return _PointerStorage[Index];
    }

    /**
     * Pops element from the array.
     *
     * @param bAllowShrinking If this call allows shrinking of the array during element remove.
     * @returns Popped element.
     */
    ULANG_FORCEINLINE PointerType Pop(bool bAllowShrinking = true)
    {
        return PointerType(_PointerStorage.Pop(bAllowShrinking), _PointerStorage.GetRawAllocator());
    }

    /**
     * Pushes element into the array.
     *
     * @param Item Item to push.
     */
    ULANG_FORCEINLINE void Push(PointerType && Item)
    {
        ULANG_ASSERTF(_PointerStorage.GetRawAllocator() == Item.GetAllocator(), "Allocators must be compatible.");
        _PointerStorage.Push(Item.Get());
        Item._Object = nullptr;
    }

    /**
     * Returns the top element, i.e. the last one.
     *
     * @returns Naked pointer to the top element.
     * @see Pop, Push
     */
    ULANG_FORCEINLINE ElementType* Top() const
    {
        return _PointerStorage.Top();
    }

    /**
     * Returns n-th last element from the array.
     *
     * @param IndexFromTheEnd (Optional) Index from the end of array (default = 0).
     * @returns Reference to n-th last element from the array.
     */
    ULANG_FORCEINLINE ElementType* Last(int32_t IndexFromTheEnd = 0) const
    {
        return _PointerStorage.Last(IndexFromTheEnd);
    }

    /**
     * Shrinks the array's used pointer memory to smallest possible to store elements currently in it.
     *
     * @see Slack
     */
    ULANG_FORCEINLINE void Shrink()
    {
        _PointerStorage.Shrink();
    }

    /**
     * Finds element within the array (by address comparison).
     *
     * @param Item Item to look for.
     * @param Index Will contain the found index.
     * @returns True if found. False otherwise.
     * @see FindLast, FindLastByPredicate
     */
    ULANG_FORCEINLINE bool Find(ElementType * Item, int32_t& Index) const
    {
        return _PointerStorage.Find(Item, Index);
    }

    /**
     * Finds element within the array (by address comparison).
     *
     * @param Item Item to look for.
     * @returns Index of the found element. IndexNone otherwise.
     * @see FindLast, FindLastByPredicate
     */
    ULANG_FORCEINLINE int32_t Find(ElementType * Item) const
    {
        return _PointerStorage.Find(Item);
    }

    /**
     * Finds element within the array starting from the end (by address comparison).
     *
     * @param Item Item to look for.
     * @param Index Output parameter. Found index.
     * @returns True if found. False otherwise.
     * @see Find, FindLastByPredicate
     */
    ULANG_FORCEINLINE bool FindLast(const PointerType & Item, int32_t& Index) const
    {
        return _PointerStorage.FindLast(Item.Get(), Index);
    }

    /**
     * Finds element within the array starting from the end (by address comparison).
     *
     * @param Item Item to look for.
     * @returns Index of the found element. IndexNone otherwise.
     */
    ULANG_FORCEINLINE int32_t FindLast(const PointerType & Item) const
    {
        return _PointerStorage.FindLast(Item.Get());
    }

    /**
     * Searches an initial subrange of the array for the last occurrence of an element which matches the specified predicate.
     *
     * @param Pred Predicate taking const pointer to array element and returns true if element matches search criteria, false otherwise.
     * @param Count The number of elements from the front of the array through which to search.
     * @returns Index of the found element. IndexNone otherwise.
     */
    template <typename Predicate>
    ULANG_FORCEINLINE int32_t FindLastByPredicate(Predicate Pred, int32_t Count) const
    {
        return _PointerStorage.FindLastByPredicate(Pred, Count);
    }

    /**
     * Searches the array for the last occurrence of an element which matches the specified predicate.
     *
     * @param Pred Predicate taking const pointer to array element and returns true if element matches search criteria, false otherwise.
     * @returns Index of the found element. IndexNone otherwise.
     */
    template <typename Predicate>
    ULANG_FORCEINLINE int32_t FindLastByPredicate(Predicate Pred) const
    {
        return _PointerStorage.FindLastByPredicate(Pred);
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
        return _PointerStorage.IndexOfByPredicate([=](ElementType * Item) -> bool { return *Item == Key; });
    }

    /**
     * Finds an item by predicate.
     *
     * @param Pred The predicate to match, taking const pointer to array element.
     * @returns Index to the first matching element, or IndexNone if none is found.
     */
    template <typename Predicate>
    int32_t IndexOfByPredicate(Predicate Pred) const
    {
        return _PointerStorage.IndexOfByPredicate(Pred);
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
    ULANG_FORCEINLINE ElementType* FindByKey(const KeyType& Key) const
    {
        ElementType * const * Element = _PointerStorage.FindByPredicate([=](ElementType * Item) -> bool { return *Item == Key; });
        return Element ? *Element : nullptr;
    }

    /**
     * Finds an element which matches a predicate functor.
     *
     * @param Pred The functor to apply to each element, taking const pointer to array element.
     * @returns Pointer to the first element for which the predicate returns true, or nullptr if none is found.
     * @see FilterByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    ULANG_FORCEINLINE ElementType* FindByPredicate(Predicate Pred) const
    {
        ElementType * const * Element = _PointerStorage.FindByPredicate(Pred);
        return Element ? *Element : nullptr;
    }

    /**
     * Checks if this array contains the exact pointer.
     *
     * @returns True if found. False otherwise.
     * @see ContainsByPredicate, FilterByPredicate, FindByPredicate
     */
    bool Contains(const ElementType* Pointer) const
    {
        return _PointerStorage.FindByPredicate([=](ElementType * Item) -> bool { return Item == Pointer; }) != nullptr;
    }

    /**
     * Checks if this array contains the element.
     *
     * @returns True if found. False otherwise.
     * @see ContainsByPredicate, FilterByPredicate, FindByPredicate
     */
    template <typename ComparisonType>
    bool ContainsByKey(const ComparisonType& Key) const
    {
        return _PointerStorage.FindByPredicate([=](ElementType * Item) -> bool { return *Item == Key; }) != nullptr;
    }

    /**
     * Checks if this array contains element for which the predicate is true.
     *
     * @param Predicate to use, taking const pointer to array element
     * @returns True if found. False otherwise.
     * @see Contains, Find
     */
    template <typename Predicate>
    ULANG_FORCEINLINE bool ContainsByPredicate(Predicate Pred) const
    {
        return _PointerStorage.ContainsByPredicate(Pred);
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
        _PointerStorage.InsertZeroed(Index, Count);
    }

    /**
     * Inserts given elements into the array at given location.
     *
     * @param Items Array of elements to insert.
     * @param InIndex Tells where to insert the new elements.
     * @returns Location at which the item was inserted.
     */
    int32_t Insert(TUPtrArrayG&& Items, const int32_t InIndex)
    {
        return _PointerStorage.Insert(ForwardArg<PointerStorageType>(Items._PointerStorage), InIndex);
    }

    /**
     * Inserts a given element into the array at given location.
     *
     * @param Item The element to insert.
     * @param Index Tells where to insert the new elements.
     * @returns Location at which the insert was done.
     * @see Add, Remove
     */
    int32_t Insert(PointerType&& Item, int32_t Index)
    {
        ElementType* ItemPtr = Item.Get();
        Item._Object = nullptr;
        return _PointerStorage.Insert(ItemPtr, Index);
    }

    /**
     * Removes an element (or elements) at given location optionally shrinking
     * the array.
     *
     * @param Index Location in array of the element to remove.
     */
    ULANG_FORCEINLINE PointerType RemoveAt(int32_t Index)
    {
        PointerType RemovedItem(_PointerStorage[Index], _PointerStorage.GetRawAllocator());
        _PointerStorage.RemoveAt(Index);
        return uLang::Move(RemovedItem);
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
        DeleteRange(Index, Index + int32_t(Count));
        _PointerStorage.RemoveAt(Index, Count, bAllowShrinking);
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
    ULANG_FORCEINLINE PointerType RemoveAtSwap(int32_t Index)
    {
        PointerType RemovedItem(_PointerStorage[Index], _PointerStorage.GetRawAllocator());
        _PointerStorage.RemoveAtSwap(Index);
        return uLang::Move(RemovedItem);
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
        DeleteRange(Index, Index + int32_t(Count));
        _PointerStorage.RemoveAtSwap(Index, Count, bAllowShrinking);
    }

    /**
     * Replaces a given element at a given location.
     *
     * @param Item The element to insert.
     * @param Index Tells where to insert the new element.
     * @returns Location at which the replacement was done.
     * @see Add, Remove
     */
    int32_t ReplaceAt(PointerType&& Item, int32_t Index)
    {
        Delete(Index);
        ElementType* ItemPtr = Item.Get();
        Item._Object = nullptr;
        _PointerStorage[Index] = ItemPtr;
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
        DeleteAll();
        _PointerStorage.Reset(NewSize);
    }

    /**
     * Empties the array. It calls the destructors on held items if needed.
     *
     * @param Slack (Optional) The expected usage size after empty operation. Default is 0.
     */
    void Empty(int32_t Slack = 0)
    {
        DeleteAll();
        _PointerStorage.Empty(Slack);
    }

    /**
     * Resizes array to given number of elements. New elements will be zeroed.
     *
     * @param NewNum New size of the array.
     */
    void SetNumZeroed(int32_t NewNum, bool bAllowShrinking = true)
    {
        if (NewNum < Num())
        {
            DeleteRange(NewNum, Num());
        }
        _PointerStorage.SetNumZeroed(NewNum, bAllowShrinking);
    }

    /**
     * Appends the specified array to this array.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    void Append(TUPtrArrayG&& Source)
    {
        _PointerStorage.Append(ForwardArg<PointerStorageType>(Source._PointerStorage));
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * Move semantics version.
     *
     * @param Other The array to append.
     */
    TUPtrArrayG& operator+=(TUPtrArrayG&& Other)
    {
        Append(ForwardArg<TUPtrArrayG>(Other));
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
        return _PointerStorage.Emplace(Item);
    }

    /**
     * Constructs a new item at a specified index, possibly reallocating the whole array to fit.
     *
     * @param Index The index to add the item at.
     * @param Args  The arguments to forward to the constructor of the new item.
     */
    template <typename... CtorArgsType>
    ULANG_FORCEINLINE void InsertNew(int32_t Index, CtorArgsType&&... CtorArgs)
    {
        ElementType * Item = new(_PointerStorage.GetRawAllocator()) ElementType(uLang::ForwardArg<CtorArgsType>(CtorArgs)...);
        _PointerStorage.EmplaceAt(Index, Item);
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
    ULANG_FORCEINLINE int32_t Add(PointerType&& Item)
    {
        ElementType* ItemPtr = Item.Get();
        Item._Object = nullptr;
        return _PointerStorage.Emplace(ItemPtr);
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
        return _PointerStorage.AddZeroed();
    }

    /**
     * Adds unique element to array if it doesn't exist.
     *
     * @param Args Item to add.
     * @returns Index of the element in the array.
     * @see Add, AddDefaulted, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t AddUnique(PointerType&& Item)
    {
        int32_t NumPrev = _PointerStorage.Num();
        int32_t Index = _PointerStorage.AddUnique(Item.Get());
        if (_PointerStorage.Num() > NumPrev)
        {
            Item._Object = nullptr;
        }
        return Index;
    }

    /**
     * Reserves memory such that the array can contain at least Number elements.
     *
     * @param Number The number of elements that the array should be able to contain after allocation.
     * @see Shrink
     */
    ULANG_FORCEINLINE void Reserve(int32_t Number)
    {
        _PointerStorage.Reserve(Number);
    }

    /**
     * Remove all instances that match the predicate, maintaining order but not indices
     * Optimized to work with runs of matches/non-matches
     *
     * @param Pred Predicate, taking const pointer to array element
     * @returns Number of removed elements.
     * @see Add, Insert, RemoveAllSwap, RemoveSingle, RemoveSwap
     */
    template <typename Predicate>
    int32_t RemoveAll(const Predicate& Pred)
    {
        const typename ElementAllocatorType::RawAllocatorType & RawAllocator = _PointerStorage.GetRawAllocator();
        return _PointerStorage.RemoveAll([&](ElementType* Item) -> bool {
            bool RemoveIt = Pred(Item);
            if (RemoveIt)
            {
                Item->~ElementType();
                RawAllocator.Deallocate(Item);
            }
            return RemoveIt;
        });
    }

    /**
     * Remove all instances that match the predicate
     *
     * @param Pred Predicate, taking const pointer to array element
     * @see Remove, RemoveSingle, RemoveSingleSwap, RemoveSwap
     */
    template <class Predicate>
    void RemoveAllSwap(const Predicate& Pred, bool bAllowShrinking = true)
    {
        const typename ElementAllocatorType::RawAllocatorType & RawAllocator = _PointerStorage.GetRawAllocator();
        return _PointerStorage.RemoveAllSwap([&](ElementType* Item) -> bool {
            bool RemoveIt = Pred(Item);
            if (RemoveIt)
            {
                Item->~ElementType();
                RawAllocator.Deallocate(Item);
            }
            return RemoveIt;
        });
    }

    /**
     * Sorts the array assuming < operator is defined for ElementType.
     */
    void Sort()
    {
        Algo::Sort( _PointerStorage, TDereferenceWrapper<TLess<ElementType>>( TLess<ElementType>() ) );
    }

    /**
     * Sorts the array using user define predicate class.
     *
     * @param Predicate Predicate instance or lambda, taking const pointer to array element
     */
    template <class PredicateType>
    void Sort(const PredicateType& Predicate)
    {
        Algo::Sort( _PointerStorage, Predicate );
    }

    /**
     * Casts TUPtrArray<T>& -> TUPtrArray<U>& if T is castable to U.
     */
    template<class OtherElementType, bool OtherAllowNull = AllowNull, typename = typename TEnableIf<TPointerIsStaticCastableFromTo<OtherElementType, ElementType>::Value && (AllowNull == OtherAllowNull || OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TUPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...>& As() { return *reinterpret_cast<TUPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...> *>(this); }
    template<class OtherElementType, bool OtherAllowNull = AllowNull, typename = typename TEnableIf<TPointerIsStaticCastableFromTo<OtherElementType, ElementType>::Value && (AllowNull == OtherAllowNull || OtherAllowNull)>::Type>
    ULANG_FORCEINLINE const TUPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...>& As() const { return *reinterpret_cast<const TUPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...> *>(this); }

public:

    /**
     * DO NOT USE DIRECTLY
     * STL-like iterators to enable range-based for loop support.
     */
    ULANG_FORCEINLINE ElementType **        begin()       { return _PointerStorage.begin(); }
    ULANG_FORCEINLINE ElementType * const * begin() const { return _PointerStorage.begin(); }
    ULANG_FORCEINLINE ElementType **        end()         { return _PointerStorage.end(); }
    ULANG_FORCEINLINE ElementType * const * end() const   { return _PointerStorage.end(); }

protected:

    /** Delete a single element */
    ULANG_FORCEINLINE void Delete(int32_t Index)
    {
        ElementType * Item = _PointerStorage[Index];
        if (Item)
        {
            Item->~ElementType();
            _PointerStorage.GetRawAllocator().Deallocate(Item);
        }
    }

    /** Delete a range of elements */
    ULANG_FORCEINLINE void DeleteRange(int32_t BeginIndex, int32_t EndIndex)
    {
        for (int32_t Index = BeginIndex; Index < EndIndex; ++Index)
        {
            Delete(Index);
        }
    }

    /** Delete all elements */
    ULANG_FORCEINLINE void DeleteAll()
    {
        DeleteRange(0, _PointerStorage.Num());
    }

    PointerStorageType _PointerStorage;

};

/// Array of unique pointers that allocates elements on the heap
template<typename ElementType>
using TUPtrArray = TUPtrArrayG<ElementType, true, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Array of unique references that allocates elements on the heap
template<typename ElementType>
using TURefArray = TUPtrArrayG<ElementType, false, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Array of unique pointers that allocates object using a given allocator instance
template<typename ElementType>
using TUPtrArrayA = TUPtrArrayG<ElementType, true, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;

/// Array of unique references that allocates object using a given allocator instance
template<typename ElementType>
using TURefArrayA = TUPtrArrayG<ElementType, false, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;


template <typename ElementType, bool AllowNull, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsZeroConstructType<TUPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = TAllocatorTraits<ElementAllocatorType>::IsZeroConstruct };
};

template <typename ElementType, bool AllowNull, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TContainerTraits<TUPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>> : public TContainerTraitsBase<TUPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    static_assert(TAllocatorTraits<ElementAllocatorType>::SupportsMove, "TUPtrArray no longer supports move-unaware allocators");
    enum { MoveWillEmptyContainer = TAllocatorTraits<ElementAllocatorType>::SupportsMove };
};

template <typename ElementType, bool AllowNull, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsContiguousContainer<TUPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = true };
};

}
