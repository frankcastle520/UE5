// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Templates/Storage.h"

#include <initializer_list>
#include <type_traits>

namespace uLang
{

/**
 * Templated dynamic array of shared pointers to elements
 **/
template<typename InElementType, bool AllowNull, typename InElementAllocatorType, typename... RawAllocatorArgsType>
class TSPtrArrayG
{
    template <typename OtherElementType, bool OtherAllowNull, typename OtherElementAllocatorType, typename... OtherRawAllocatorArgsType>
    friend class TSPtrArrayG;

public:

    using ElementType = InElementType;
    using ElementAllocatorType = InElementAllocatorType;
    using PointerType = TSPtrG<ElementType, AllowNull, typename InElementAllocatorType::RawAllocatorType, RawAllocatorArgsType...>;
    using PointerStorageType = TArrayG<ElementType *, InElementAllocatorType, RawAllocatorArgsType...>;

    /**
     * Constructor
     */
    ULANG_FORCEINLINE TSPtrArrayG(RawAllocatorArgsType&&... RawAllocatorArgs)
        : _PointerStorage(uLang::ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
        , _DereferenceFunc(nullptr)
    {}

    /**
     * Initializer list constructor
     */
    template<typename OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrArrayG(std::initializer_list<TSPtrG<OtherElementType, OtherAllowNull, typename InElementAllocatorType::RawAllocatorType, RawAllocatorArgsType...>> InitList, RawAllocatorArgsType&&... RawAllocatorArgs)
        : _PointerStorage(uLang::ForwardArg<RawAllocatorArgsType>(RawAllocatorArgs)...)
        , _DereferenceFunc(nullptr)
    {
        for(const TSPtrG<OtherElementType, OtherAllowNull, typename InElementAllocatorType::RawAllocatorType, RawAllocatorArgsType...>& Element : InitList)
        {
            Add(Element);
        }
    }

    /**
     * Implicitly casting copy-ish constructor.
     *
     * @param Other The source array to copy.
     */
    template<typename OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrArrayG(const TSPtrArrayG<OtherElementType, OtherAllowNull, InElementAllocatorType, RawAllocatorArgsType...>& Other)
        : _PointerStorage(Other._PointerStorage)
    {
        ReferenceAll();
        EnableDereference();
    }

    /**
     * Copy constructor.
     *
     * @param Other The source array to copy.
     */
    ULANG_FORCEINLINE TSPtrArrayG(const TSPtrArrayG& Other)
        : _PointerStorage(Other._PointerStorage)
    {
        ReferenceAll();
        EnableDereference();
    }

    /**
     * Implicitly casting copy-ish constructor with extra slack.
     *
     * @param Other The source array to copy.
     * @param ExtraSlack Tells how much extra memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    template<typename OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrArrayG(const TSPtrArrayG<OtherElementType, OtherAllowNull, InElementAllocatorType, RawAllocatorArgsType...>& Other, int32_t ExtraSlack)
        : _PointerStorage(Other._PointerStorage, ExtraSlack)
    {
        ReferenceAll();
        EnableDereference();
    }

    /**
     * Implicitly casting copying assignment-ish operator. First deletes all currently contained elements
     * and then copies from other array.
     *
     * @param Other The source array to assign from.
     */
    template<typename OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    TSPtrArrayG& operator=(const TSPtrArrayG<OtherElementType, OtherAllowNull, InElementAllocatorType, RawAllocatorArgsType...>& Other)
    {
        if (this != &Other)
        {
            _PointerStorage = Other._PointerStorage;
            ReferenceAll();
            EnableDereference();
        }
        return *this;
    }
    
    /**
     * Copying assignment operator. First deletes all currently contained elements
     * and then copies from other array.
     *
     * @param Other The source array to assign from.
     */
    TSPtrArrayG& operator=(const TSPtrArrayG& Other)
    {
        if (this != &Other)
        {
            _PointerStorage = Other._PointerStorage;
            ReferenceAll();
            EnableDereference();
        }
        return *this;
    }

    /**
     * Implicitly casting move-ish constructor.
     *
     * @param Other Array to move from.
     */
    template<typename OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrArrayG(TSPtrArrayG<OtherElementType, OtherAllowNull, InElementAllocatorType, RawAllocatorArgsType...>&& Other)
        : _PointerStorage(ForwardArg<PointerStorageType>(Other._PointerStorage))
    {
        EnableDereference();
    }
    
    /**
     * Move constructor.
     *
     * @param Other Array to move from.
     */
    ULANG_FORCEINLINE TSPtrArrayG(TSPtrArrayG&& Other)
        : _PointerStorage(ForwardArg<PointerStorageType>(Other._PointerStorage))
    {
        EnableDereference();
    }

    /**
     * Implicitly casting move-ish constructor with extra slack.
     *
     * @param Other Array to move from.
     * @param ExtraSlack Tells how much extra pointer memory should be preallocated
     *                   at the end of the array in the number of elements.
     */
    template<class OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    TSPtrArrayG(TSPtrArrayG<OtherElementType, OtherAllowNull, InElementAllocatorType, RawAllocatorArgsType...>&& Other, int32_t ExtraSlack)
        : _PointerStorage(ForwardArg<PointerStorageType>(Other._PointerStorage), ExtraSlack)
    {
        EnableDereference();
    }

    /**
     * Implicitly casting move assignment operator.
     *
     * @param Other Array to assign and move from.
     */
    template<class OtherElementType, bool OtherAllowNull, typename = typename TEnableIf<std::is_convertible_v<OtherElementType*, InElementType*> && (AllowNull || !OtherAllowNull)>::Type>
    TSPtrArrayG& operator=(TSPtrArrayG<OtherElementType, OtherAllowNull, InElementAllocatorType, RawAllocatorArgsType...>&& Other)
    {
        if (this != &Other)
        {
            DereferenceAll();
            _PointerStorage = ForwardArg<PointerStorageType>(Other._PointerStorage);
            EnableDereference();
        }
        return *this;
    }

    /**
     * Move assignment operator.
     *
     * @param Other Array to assign and move from.
     */
    TSPtrArrayG& operator=(TSPtrArrayG&& Other)
    {
        if (this != &Other)
        {
            DereferenceAll();
            _PointerStorage = ForwardArg<PointerStorageType>(Other._PointerStorage);
            EnableDereference();
        }
        return *this;
    }

    /** Destructor. */
    ~TSPtrArrayG()
    {
        DereferenceAll();
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
     * Array bracket operator. Returns shared pointer to element at given index.
     */
    ULANG_FORCEINLINE PointerType operator[](int32_t Index) const
    {
        return PointerType(_PointerStorage[Index], _PointerStorage.GetRawAllocator());
    }

    /**
     * Pops element from the array.
     *
     * @param bAllowShrinking If this call allows shrinking of the array during element remove.
     * @returns Popped element.
     */
    ULANG_FORCEINLINE PointerType Pop(bool bAllowShrinking = true)
    {
        PointerType Item(_PointerStorage.Pop(bAllowShrinking), _PointerStorage.GetRawAllocator());
        if (Item)
        {
            Item->Dereference();
        }
        return Item;
    }

    /**
     * Pushes element into the array.
     *
     * @param Item Item to push.
     */
    ULANG_FORCEINLINE void Push(const PointerType & Item)
    {
        if (Item)
        {
            Item->Reference();
        }
        _PointerStorage.Push(Item.Get());
        EnableDereference();
    }

    /**
     * Returns the first element.
     *
     * @returns Reference to the first element.
     * @see Last, Pop, Push
     */
    ULANG_FORCEINLINE PointerType First() const
    {
        return PointerType(*_PointerStorage.GetData(), _PointerStorage.GetRawAllocator());
    }

    /**
     * Returns the top element, i.e. the last one.
     *
     * @returns Reference to the top element.
     * @see Pop, Push
     */
    ULANG_FORCEINLINE PointerType Top() const
    {
        return PointerType(_PointerStorage.Top(), _PointerStorage.GetRawAllocator());
    }

    /**
     * Returns n-th last element from the array.
     *
     * @param IndexFromTheEnd (Optional) Index from the end of array (default = 0).
     * @returns Reference to n-th last element from the array.
     */
    ULANG_FORCEINLINE PointerType Last(int32_t IndexFromTheEnd = 0) const
    {
        return PointerType(_PointerStorage.Last(IndexFromTheEnd), _PointerStorage.GetRawAllocator());
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
    ULANG_FORCEINLINE bool Find(const ElementType* Item, int32_t& Index) const
    {
        return _PointerStorage.Find(const_cast<ElementType*>(Item), Index);
    }
    ULANG_FORCEINLINE bool Find(const PointerType& Item, int32_t& Index) const
    {
        return Find(Item.Get(), Index);
    }


    /**
     * Finds element within the array (by address comparison).
     *
     * @param Item Item to look for.
     * @returns Index of the found element. IndexNone otherwise.
     * @see FindLast, FindLastByPredicate
     */
    ULANG_FORCEINLINE int32_t Find(const ElementType* Item) const
    {
        return _PointerStorage.Find(const_cast<ElementType*>(Item));
    }
    ULANG_FORCEINLINE int32_t Find(const PointerType& Item) const
    {
        return Find(Item.Get());
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
    ULANG_FORCEINLINE TOptional<PointerType> FindByKey(const KeyType& Key) const
    {
        ElementType * const * Element = _PointerStorage.FindByPredicate([=](ElementType * Item) -> bool { return *Item == Key; });
        if (Element)
        {
            return PointerType(*Element, _PointerStorage.GetRawAllocator());
        }

        return EResult::Unspecified;
    }

    /**
     * Finds an element which matches a predicate functor.
     *
     * @param Pred The functor to apply to each element, taking const pointer to array element.
     * @returns Pointer to the first element for which the predicate returns true, or nullptr if none is found.
     * @see FilterByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    ULANG_FORCEINLINE TOptional<PointerType> FindByPredicate(Predicate Pred) const
    {
        ElementType * const * Element = _PointerStorage.FindByPredicate(Pred);
        if (Element)
        {
            return PointerType(*Element, _PointerStorage.GetRawAllocator());
        }

        return EResult::Unspecified;
    }

    /**
     * Checks if this array contains the exact pointer.
     *
     * @returns True if found. False otherwise.
     * @see ContainsByPredicate, FilterByPredicate, FindByPredicate
     */
    bool Contains(const PointerType& Pointer) const
    {
        return _PointerStorage.FindByPredicate([=](ElementType * Item) -> bool { return Item == Pointer.Get(); }) != nullptr;
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
        EnableDereference();
    }

    /**
     * Inserts given elements into the array at given location.
     *
     * @param Items Array of elements to insert.
     * @param InIndex Tells where to insert the new elements.
     * @returns Location at which the item was inserted.
     */
    int32_t Insert(const TSPtrArrayG& Items, const int32_t InIndex)
    {
        _PointerStorage.Insert(Items._PointerStorage, InIndex);
        ReferenceRange(InIndex, InIndex + Items.Num());
        EnableDereference();
        return InIndex;
    }

    /**
     * Inserts given elements into the array at given location.
     *
     * @param Items Array of elements to insert.
     * @param InIndex Tells where to insert the new elements.
     * @returns Location at which the item was inserted.
     */
    int32_t Insert(TSPtrArrayG&& Items, const int32_t InIndex)
    {
        EnableDereference();
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
    int32_t Insert(const PointerType& Item, int32_t Index)
    {
        if (Item)
        {
            Item->Reference();
        }
        EnableDereference();
        return _PointerStorage.Insert(Item.Get(), Index);
    }

    /**
     * Removes an element (or elements) at given location optionally shrinking
     * the array.
     *
     * @param Index Location in array of the element to remove.
     * @param Count (Optional) Number of elements to remove. Default is 1.
     * @param bAllowShrinking (Optional) Tells if this call can shrink array if suitable after remove. Default is true.
     * @returns The element removed from the array.
     */
    ULANG_FORCEINLINE PointerType RemoveAt(int32_t Index)
    {
        PointerType RemovedItem(_PointerStorage[Index], _PointerStorage.GetRawAllocator());
        _PointerStorage.RemoveAt(Index);
        if (RemovedItem)
        {
            // Decrement the reference counter for the pointer removed from the array.
            RemovedItem->Dereference();
        }
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
        DereferenceRange(Index, Index + int32_t(Count));
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
    ULANG_FORCEINLINE void RemoveAtSwap(int32_t Index)
    {
        Dereference(Index);
        _PointerStorage.RemoveAtSwap(Index);
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
        DereferenceRange(Index, Index + int32_t(Count));
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
    int32_t ReplaceAt(const PointerType& Item, int32_t Index)
    {
        if (Item)
        {
            Item->Reference();
        }
        EnableDereference();
        Dereference(Index);
        _PointerStorage[Index] = Item.Get();
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
        DereferenceAll();
        _PointerStorage.Reset(NewSize);
    }

    /**
     * Empties the array. It calls the destructors on held items if needed.
     *
     * @param Slack (Optional) The expected usage size after empty operation. Default is 0.
     */
    void Empty(int32_t Slack = 0)
    {
        DereferenceAll();
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
            DereferenceRange(NewNum, Num());
        }
        _PointerStorage.SetNumZeroed(NewNum, bAllowShrinking);
    }

    /**
     * Appends the specified array to this array.
     *
     * Allocator changing version.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    void Append(TSPtrArrayG& Source)
    {
        Source.ReferenceAll();
        _PointerStorage.Append(Source._PointerStorage);
        EnableDereference();
    }

    /**
     * Appends the specified array to this array.
     *
     * @param Source The array to append.
     * @see Add, Insert
     */
    void Append(TSPtrArrayG&& Source)
    {
        _PointerStorage.Append(ForwardArg<PointerStorageType>(Source._PointerStorage));
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
    TSPtrArrayG& operator+=(TSPtrArrayG&& Other)
    {
        Append(ForwardArg<TSPtrArrayG>(Other));
        return *this;
    }

    /**
     * Appends the specified array to this array.
     * Cannot append to self.
     *
     * @param Other The array to append.
     */
    TSPtrArrayG& operator+=(const TSPtrArrayG& Other)
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
        Item->Reference();
        EnableDereference();
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
    ULANG_FORCEINLINE int32_t Add(const PointerType& Item)
    {
        if (Item)
        {
            Item->Reference();
        }
        EnableDereference();
        return _PointerStorage.Emplace(Item.Get());
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
        EnableDereference();
        return _PointerStorage.AddZeroed();
    }

    /**
     * Adds unique element to array if it doesn't exist.
     *
     * @param Args Item to add.
     * @returns Index of the element in the array.
     * @see Add, AddDefaulted, AddZeroed, Append, Insert
     */
    ULANG_FORCEINLINE int32_t AddUnique(const PointerType& Item)
    {
        int32_t NumPrev = _PointerStorage.Num();
        int32_t Index = _PointerStorage.AddUnique(Item.Get());
        if (Item && _PointerStorage.Num() > NumPrev)
        {
            Item->Reference();
        }
        EnableDereference();
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
     * Removes the first occurrence of the specified item in the array,
     * maintaining order but not indices.
     *
     * @param Item The item to remove.
     * @returns The number of items removed. For RemoveSingleItem, this is always either 0 or 1.
     * @see Add, Insert, Remove, RemoveAll, RemoveAllSwap
     */
    int32_t RemoveSingle(const PointerType& Item)
    {
        int32_t Num = _PointerStorage.RemoveSingle(Item.Get());
        if (Num)
        {
            ULANG_ENSUREF(!Item->Dereference(), "Item was passed in so there must be at least one reference left after this.");
        }
        return Num;
    }

    /**
     * Removes as many instances of Item as there are in the array, maintaining
     * order but not indices.
     *
     * @param Item Item to remove from array.
     * @returns Number of removed elements.
     * @see Add, Insert, RemoveAll, RemoveAllSwap, RemoveSingle, RemoveSwap
     */
    int32_t Remove(const PointerType& Item)
    {
        int32_t Num = _PointerStorage.Remove(Item.Get());
        for (int32_t Count = Num; Count; --Count)
        {
            ULANG_ENSUREF(!Item->Dereference(), "Item was passed in so there must be at least one reference left after this.");
        }
        return Num;
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
            if (RemoveIt && Item->Dereference())
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
            if (RemoveIt && Item->Dereference())
            {
                Item->~ElementType();
                RawAllocator.Deallocate(Item);
            }
            return RemoveIt;
        });
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
    int32_t RemoveSingleSwap(const PointerType& Item, bool bAllowShrinking = true)
    {
        int32_t Num = _PointerStorage.RemoveSingleSwap(Item.Get());
        if (Num)
        {
            ULANG_ENSUREF(!Item->Dereference(), "Item was passed in so there must be at least one reference left after this.");
        }
        return Num;
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
    int32_t RemoveSwap(const PointerType& Item)
    {
        int32_t Num = _PointerStorage.RemoveSwap(Item.Get());
        for (int32_t Count = Num; Count; --Count)
        {
            ULANG_ENSUREF(!Item->Dereference(), "Item was passed in so there must be at least one reference left after this.");
        }
        return Num;
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
     * Casts TSPtrArray<T>& -> TSPtrArray<U>& if T is castable to U.
     */
    template<class OtherElementType, bool OtherAllowNull = AllowNull, typename = typename TEnableIf<TPointerIsStaticCastableFromTo<OtherElementType, ElementType>::Value && (AllowNull == OtherAllowNull || OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...>& As() { return *reinterpret_cast<TSPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...> *>(this); }
    template<class OtherElementType, bool OtherAllowNull = AllowNull, typename = typename TEnableIf<TPointerIsStaticCastableFromTo<OtherElementType, ElementType>::Value && (AllowNull == OtherAllowNull || OtherAllowNull)>::Type>
    ULANG_FORCEINLINE const TSPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...>& As() const { return *reinterpret_cast<const TSPtrArrayG<OtherElementType, OtherAllowNull, ElementAllocatorType, RawAllocatorArgsType...> *>(this); }

public:

    /**
     * STL-like iterator to enable range-based for loop support.
     */

    class Iterator
    {
    public:
        ULANG_FORCEINLINE Iterator() : _Item(nullptr) {} //-V730
        ULANG_FORCEINLINE Iterator(ElementType * const * Item, const typename InElementAllocatorType::RawAllocatorType & Allocator) : _Item(Item) { Ptr()._Allocator = Allocator; } //-V730
        ULANG_FORCEINLINE ~Iterator() = default;
        ULANG_FORCEINLINE const PointerType& operator*() const { ULANG_ASSERT(_Item); Ptr()._Object = *_Item; Ptr().EnableRelease(); return Ptr(); }
        ULANG_FORCEINLINE const ElementType* operator->() const { ULANG_ASSERT(_Item); return *_Item; }
        ULANG_FORCEINLINE bool               operator==(const Iterator & Other) const { return _Item == Other._Item; }
        ULANG_FORCEINLINE bool               operator!=(const Iterator & Other) const { return _Item != Other._Item; }
        ULANG_FORCEINLINE Iterator&          operator++()    { ++_Item; return *this; }
        ULANG_FORCEINLINE Iterator           operator++(int) { Iterator Temp = *this; ++*this; return Temp; }
        ULANG_FORCEINLINE Iterator&          operator--()    { --_Item; return *this; }
        ULANG_FORCEINLINE Iterator           operator--(int) { Iterator Temp = *this; --*this; return Temp; }
        friend ULANG_FORCEINLINE Iterator    operator+(const Iterator& Left, int32_t Right) { Iterator Temp = Left; Temp._Item += Right; return Temp; }
        friend ULANG_FORCEINLINE Iterator    operator+(int32_t Left, const Iterator& Right) { Iterator Temp = Right; Temp._Item += Left; return Temp; }
        friend ULANG_FORCEINLINE int32_t     operator-(const Iterator& Left, const Iterator& Right) { return static_cast<int32_t>(Left._Item - Right._Item); }

    protected:
        ULANG_FORCEINLINE PointerType& Ptr() const { return *(PointerType*)&_DummyPtr; }

        ElementType * const *             _Item;
        TTypeCompatibleBytes<PointerType> _DummyPtr; // Dummy shared pointer to avoid copy & unnecessary reference/dereference of object
    };

    ULANG_FORCEINLINE Iterator begin()       { return Iterator(_PointerStorage.begin(), _PointerStorage.GetRawAllocator()); }
    ULANG_FORCEINLINE Iterator begin() const { return Iterator(_PointerStorage.begin(), _PointerStorage.GetRawAllocator()); }
    ULANG_FORCEINLINE Iterator end()         { return Iterator(_PointerStorage.end(), _PointerStorage.GetRawAllocator()); }
    ULANG_FORCEINLINE Iterator end() const   { return Iterator(_PointerStorage.end(), _PointerStorage.GetRawAllocator()); }

protected:

    /** Set the release function pointer to a valid value */
    ULANG_FORCEINLINE void EnableDereference()
    {
        _DereferenceFunc = [](ElementType * Item, const typename ElementAllocatorType::RawAllocatorType & Allocator)
            {
                if (Item && Item->Dereference())
                {
                    // No references left: Delete the element
                    Item->~ElementType();
                    Allocator.Deallocate(Item);
                }
            };
    }

    /** Decrement reference count on an element */
    ULANG_FORCEINLINE void Dereference(int32_t Index)
    {
        // Call the function set by EnableDereference() above
        (*_DereferenceFunc)(_PointerStorage[Index], _PointerStorage.GetRawAllocator());
    }

    /** Increment reference count on a range of elements */
    ULANG_FORCEINLINE void ReferenceRange(int32_t BeginIndex, int32_t EndIndex)
    {
        for (int32_t Index = BeginIndex; Index < EndIndex; ++Index)
        {
            ElementType * Item = _PointerStorage[Index];
            if (Item) 
            {
                Item->Reference();
            }
        }
    }

    /** Decrement reference count on a range of elements */
    ULANG_FORCEINLINE void DereferenceRange(int32_t BeginIndex, int32_t EndIndex)
    {
        for (int32_t Index = BeginIndex; Index < EndIndex; ++Index)
        {
            Dereference(Index);
        }
    }

    /** Increment reference count on all elements */
    ULANG_FORCEINLINE void ReferenceAll()
    {
        ReferenceRange(0, _PointerStorage.Num());
    }

    /** Decrement reference count on all elements. Delete elements whose reference count reaches zero. */
    ULANG_FORCEINLINE void DereferenceAll()
    {
        DereferenceRange(0, _PointerStorage.Num());
    }

    PointerStorageType _PointerStorage;

    /// Indirection to keep knowledge about ElementType out of default constructor and destructor
    /// so that TSPtrArrayG can be forward declared with an incomplete ElementType argument
    /// The price we pay is 8 more bytes of memory, indirect function call on each dereference, 
    /// and that we have to (re-)initialize this function pointer in all methods that can make an empty array non-empty
    using DereferenceFuncType = void(*)(ElementType *, const typename ElementAllocatorType::RawAllocatorType &);
    DereferenceFuncType _DereferenceFunc;
};

/// Array of shared pointers that allocates elements on the heap
template<typename ElementType>
using TSPtrArray = TSPtrArrayG<ElementType, true, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Array of shared references that allocates elements on the heap
template<typename ElementType>
using TSRefArray = TSPtrArrayG<ElementType, false, TDefaultElementAllocator<CHeapRawAllocator>>;

/// Array of shared pointers that allocates object using a given allocator instance
template<typename ElementType>
using TSPtrArrayA = TSPtrArrayG<ElementType, true, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;

/// Array of shared references that allocates object using a given allocator instance
template<typename ElementType>
using TSRefArrayA = TSPtrArrayG<ElementType, false, TDefaultElementAllocator<CInstancedRawAllocator>, CAllocatorInstance *>;


template <typename ElementType, bool AllowNull, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsZeroConstructType<TSPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = TAllocatorTraits<ElementAllocatorType>::IsZeroConstruct };
};

template <typename ElementType, bool AllowNull, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TContainerTraits<TSPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>> : public TContainerTraitsBase<TSPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    static_assert(TAllocatorTraits<ElementAllocatorType>::SupportsMove, "TSPtrArray no longer supports move-unaware allocators");
    enum { MoveWillEmptyContainer = TAllocatorTraits<ElementAllocatorType>::SupportsMove };
};

template <typename ElementType, bool AllowNull, typename ElementAllocatorType, typename... RawAllocatorArgsType>
struct TIsContiguousContainer<TSPtrArrayG<ElementType, AllowNull, ElementAllocatorType, RawAllocatorArgsType...>>
{
    enum { Value = true };
};

}
