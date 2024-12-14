// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/HashTable.h"

namespace uLang
{
    class CHeapRawAllocator;

template<class ElementType, class KeyType, class HashTraits, class AllocatorType, typename... AllocatorArgsType>
class TSetG
{
public:
    explicit TSetG(AllocatorArgsType&&... AllocatorArgs) : _HashTable(Move(AllocatorArgs)...) {}

    ULANG_FORCEINLINE uint32_t Num() const { return _HashTable.Num(); }
    ULANG_FORCEINLINE bool Contains(const ElementType& Element) const { return _HashTable.Contains(Element); }
    ULANG_FORCEINLINE ElementType* Find(const KeyType& Key) { return _HashTable.Find(Key); }
    ULANG_FORCEINLINE const ElementType* Find(const KeyType& Key) const { return _HashTable.Find(Key); }

    template <typename Predicate>
    ULANG_FORCEINLINE const ElementType* FindByPredicate(Predicate Pred) const
    {
        return _HashTable.FindByPredicate(Pred);
    }

    template <typename Predicate>
    ULANG_FORCEINLINE ElementType* FindByPredicate(Predicate Pred)
    {
        return _HashTable.FindByPredicate(Pred);
    }

    template <typename ArgType>
    ULANG_FORCEINLINE ElementType& Insert(ArgType&& Arg)
    {
        return _HashTable.Insert(ForwardArg<ArgType>(Arg));
    }

    ULANG_FORCEINLINE ElementType& FindOrInsert(ElementType&& Element) { return _HashTable.FindOrInsert(ForwardArg<ElementType>(Element)); }
    ULANG_FORCEINLINE bool Remove(const ElementType& Element) { return _HashTable.Remove(Element); }

    ULANG_FORCEINLINE bool IsEmpty() const { return _HashTable.IsEmpty(); }

    ULANG_FORCEINLINE void Empty() { _HashTable.Empty(); }

    using HashTableType = THashTable<ElementType, ElementType, HashTraits, AllocatorType, AllocatorArgsType...>;
    using ConstIterator = typename HashTableType::template Iterator<true>;
    using Iterator = typename HashTableType::template Iterator<false>;

    ULANG_FORCEINLINE Iterator begin()
    {
        return _HashTable.begin();
    }

    ULANG_FORCEINLINE Iterator end()
    {
        return _HashTable.end();
    }

    ULANG_FORCEINLINE ConstIterator begin() const
    {
        return _HashTable.cbegin();
    }

    ULANG_FORCEINLINE ConstIterator end() const
    {
        return _HashTable.cend();
    }

    ULANG_FORCEINLINE ConstIterator cbegin() const
    {
        return _HashTable.cbegin();
    }

    ULANG_FORCEINLINE ConstIterator cend() const
    {
        return _HashTable.cend();
    }

protected:
    HashTableType _HashTable;
};

// A set that assumes that the KeyType has a method `GetTypeHash()`, and that allocates memory from the heap
template<class ElementType, class KeyType = ElementType>
using TSet = TSetG<ElementType, KeyType, TDefaultHashTraits<ElementType>, CHeapRawAllocator>;

} // namespace uLang
