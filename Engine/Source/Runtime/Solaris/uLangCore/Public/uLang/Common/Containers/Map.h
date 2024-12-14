// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/HashTable.h"

namespace uLang
{
    class CHeapRawAllocator;

template<class KeyType, class ValueType, class HashTraits, class AllocatorType, typename... AllocatorArgsType>
class TMapG
{
    using PairType = TKeyValuePair<KeyType, ValueType>;

public:
    TMapG(AllocatorArgsType&&... AllocatorArgs) : _HashTable(ForwardArg<AllocatorArgsType>(AllocatorArgs)...) {}

    ULANG_FORCEINLINE uint32_t Num() const { return _HashTable.Num(); }
    ULANG_FORCEINLINE bool Contains(const KeyType& Key) const { return _HashTable.Contains(Key); }
    ULANG_FORCEINLINE ValueType* Find(const KeyType& Key) { PairType* Pair = _HashTable.Find(Key); return Pair ? &Pair->_Value : nullptr; }
    ULANG_FORCEINLINE const ValueType* Find(const KeyType& Key) const { const PairType* Pair = _HashTable.Find(Key); return Pair ? &Pair->_Value : nullptr; }

    template <typename Predicate>
    ULANG_FORCEINLINE const PairType* FindByPredicate(Predicate Pred) const
    {
        return _HashTable.FindByPredicate(Pred);
    }

    template <typename Predicate>
    ULANG_FORCEINLINE PairType* FindByPredicate(Predicate Pred)
    {
        return _HashTable.FindByPredicate(Pred);
    }

    ULANG_FORCEINLINE PairType& Insert(const KeyType& Key, ValueType&& Value) { return _HashTable.Insert({ Key, Move(Value) }); }
    ULANG_FORCEINLINE PairType& Insert(KeyType&& Key, const ValueType& Value) { return _HashTable.Insert({ Move(Key), Value }); }
    ULANG_FORCEINLINE PairType& Insert(const KeyType& Key, const ValueType& Value) { return _HashTable.Insert({ Key, ValueType(Value) }); }
    ULANG_FORCEINLINE PairType& Insert(KeyType&& Key, ValueType&& Value) { return _HashTable.Insert({ Move(Key), Move(Value) }); }
    ULANG_FORCEINLINE PairType& FindOrInsert(KeyType&& Key)
    {
        return _HashTable.FindOrInsert({ForwardArg<KeyType>(Key), ForwardArg<ValueType>(ValueType{})});
    }
    ULANG_FORCEINLINE bool Remove(const KeyType& Key) { return _HashTable.Remove(Key); }
    ULANG_FORCEINLINE void Empty() { return _HashTable.Empty(); }

    ULANG_FORCEINLINE ValueType& operator[](const KeyType& Key)
    {
        PairType* Pair = _HashTable.Find(Key);
        if (Pair)
        {
            return Pair->_Value;
        }
        else
        {
            ValueType DefaultValue{};
            KeyType NewKey(Key);
            Insert(Move(NewKey), Move(DefaultValue));
            return _HashTable.Find(Key)->_Value;
        }
    }

    using HashTableType = THashTable<KeyType, PairType, HashTraits, AllocatorType, AllocatorArgsType...>;
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

// A map that assumes that the KeyType has a method `GetTypeHash`(), and that allocates memory from the heap
template<class KeyType, class ValueType>
using TMap = TMapG<KeyType, ValueType, TDefaultHashTraits<KeyType>, CHeapRawAllocator>;

} // namespace uLang
