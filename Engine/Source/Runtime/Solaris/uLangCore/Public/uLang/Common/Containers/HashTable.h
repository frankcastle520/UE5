// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Misc/MathUtils.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Common/Templates/Storage.h" // for Swap()
#include "uLang/Common/Containers/HashTraits.h"

#include <iterator>
#include <algorithm>

namespace uLang
{

template<class KeyType, class ValueType>
struct TKeyValuePair
{
    KeyType   _Key;
    ValueType _Value;

    bool operator==(const KeyType& Key) const { return _Key == Key; }
    operator const KeyType&() const { return _Key; }
};

/// A Robin-Hood hash table
/// Inspired by https://www.sebastiansylvan.com/post/robin-hood-hashing-should-be-your-default-hash-table-implementation/
/// and http://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/
template<class KeyType, class KeyValueType, class HashTraits, class AllocatorType, typename... AllocatorArgsType>
class THashTable
{
public:
    THashTable(AllocatorArgsType&&... AllocatorArgs) 
        : _Allocator(ForwardArg<AllocatorArgsType>(AllocatorArgs)...)
    {
    }

    THashTable(const THashTable& Other) = delete;

    THashTable(THashTable&& Other)
    {
        Swap(Other);
    }

    THashTable& operator=(THashTable Other)
    {
        Swap(Other);
        return *this;
    }

    void Swap(THashTable& Other)
    {
        uLang::Swap(_Entries, Other._Entries);
        uLang::Swap(_NumEntries, Other._NumEntries);
        uLang::Swap(_NumOccupied, Other._NumOccupied);
        uLang::Swap(_Allocator, Other._Allocator);
    }

    ~THashTable()
    {
        Empty();
        if (_Entries)
        {
            _Allocator.Deallocate(_Entries);
        }
    }

    ULANG_FORCEINLINE uint32_t Num() const
    {
        return _NumOccupied;
    }

    ULANG_FORCEINLINE bool Contains(const KeyType& Key) const
    {
        return Lookup(Key) != uint32_t(IndexNone);
    }

    ULANG_FORCEINLINE KeyValueType* Find(const KeyType& Key)
    {
        uint32_t Pos = Lookup(Key);
        return Pos == uint32_t(IndexNone) ? nullptr : &_Entries[Pos]._KeyValue;
    }

    ULANG_FORCEINLINE const KeyValueType* Find(const KeyType& Key) const
    {
        uint32_t Pos = Lookup(Key);
        return Pos == uint32_t(IndexNone) ? nullptr : &_Entries[Pos]._KeyValue;
    }

    /**
     * Finds an key-value pair which matches a predicate functor.
     * The predicate must take a `TKeyValuePair<KeyType, ValueType>`.
     *
     * @param Pred The functor to apply to each key-value pair.
     * @returns Pointer to the first key-value pair for which the predicate returns true, or nullptr if none is found.
     * @see FilterByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    const KeyValueType* FindByPredicate(Predicate Pred) const
    {
        for (uint32_t Index = 0; Index < _NumEntries; ++Index)
        {
            // If the hash of the entry is `0`, it means that the slot is currently unoccupied.
            if (_Entries[Index]._Hash != 0 && Pred(_Entries[Index]._KeyValue))
            {
                return &_Entries[Index]._KeyValue;
            }
        }
        return nullptr;
    }

    /**
     * Finds a key-value pair which matches a predicate functor.
     * The predicate must take a `TKeyValuePair<KeyType, ValueType>`.
     *
     * @param Pred The functor to apply to each key-value pair. `true`, or `nullptr` if none is found.
     * @returns Pointer to the first key-value pair for which the predicate returns true, or nullptr if none is found.
     * @see FilterByPredicate, ContainsByPredicate
     */
    template <typename Predicate>
    KeyValueType* FindByPredicate(Predicate Pred)
    {
        for (uint32_t Index = 0; Index < _NumEntries; ++Index)
        {
            // If the hash of the entry is `0`, it means that the slot is currently unoccupied.
            if (_Entries[Index]._Hash != 0 && Pred(_Entries[Index]._KeyValue))
            {
                return &_Entries[Index]._KeyValue;
            }
        }
        return nullptr;
    }

    KeyValueType& Insert(const KeyValueType& KeyValue)
    {
        return InsertInternal(KeyValueType(KeyValue));
    }

    KeyValueType& Insert(KeyValueType&& KeyValue)
    {
        return InsertInternal(Move(KeyValue));
    }

    KeyValueType& FindOrInsert(KeyValueType&& KeyValue)
    {
        if (KeyValueType* Entry = Find(KeyValue))
        {
            return *Entry;
        }
        else
        {
            return Insert(Move(KeyValue));
        }
    }

    bool Remove(const KeyType& Key)
    {
        auto MoveAndDestruct = [](SEntry& Dest, SEntry& Source)
        {
            new (&Dest) SEntry(Move(Source));
            Source.~SEntry();
        };

        uint32_t Pos = Lookup(Key);
        if (Pos == uint32_t(IndexNone))
        {
            return false;
        }

        // Backward shift deletion
        // Idea is to shift backward all the entries following the entry to delete until either a vacant entry, or a entry with a probe distance of 0 is found.
        // By doing this, every deletion will shift backwards entries and therefore decrease their respective probe distances by 1.
        // An intuitive way to understand the backward shift is to think that by shifting backward the entries, the table is left as if the deleted entry had never been inserted. 
        // This is why even after a large number of deletions, the mean probe distance and the variance of the probe distance remain constant and low. 
        
        // Destruct entry's key/value
        _Entries[Pos]._KeyValue.~KeyValueType();

        // Linear probe to find stop entry
        uint32_t StopPos = (Pos + 1) & (_NumEntries - 1);
        for (;;)
        {
            SEntry& Entry = _Entries[StopPos];
            if (Entry._Hash == 0 || ProbeDistance(Entry._Hash, StopPos) == 0)
            {
                break;
            }

            StopPos = (StopPos + 1) & (_NumEntries - 1);
        }

        // Shift entries backward to fill the hole and reduce their probe distance
        if (Pos < StopPos)
        {
            for (uint32_t Index = Pos; Index + 1 < StopPos; ++Index)
            {
                MoveAndDestruct(_Entries[Index], _Entries[Index + 1]);
            }
        }
        else
        {
            // Pos wrapped around, do the move in chunks
            for (uint32_t Index = Pos; Index + 1 < _NumEntries; ++Index)
            {
                MoveAndDestruct(_Entries[Index], _Entries[Index + 1]);
            }
            if (StopPos > 0)
            {
                MoveAndDestruct(_Entries[_NumEntries - 1], _Entries[0]);
                for (uint32_t Index = 0; Index + 1 < StopPos; ++Index)
                {
                    MoveAndDestruct(_Entries[Index], _Entries[Index + 1]);
                }
            }
        }

        // Mark the hole we left behind as vacant
        _Entries[(StopPos + _NumEntries - 1) & (_NumEntries - 1)]._Hash = 0;

        // Contains one less now
        --_NumOccupied;

        return true;
    }

    bool IsEmpty() const
    {
        return _NumOccupied == 0;
    }

    void Empty()
    {
        if (_Entries)
        {
            for (uint32_t Pos = 0; Pos < _NumEntries; ++Pos)
            {
                SEntry& Entry = _Entries[Pos];
                if (Entry._Hash)
                {
                    Entry._KeyValue.~KeyValueType();
                }
            }
        }
        _NumOccupied = 0;
        _NumEntries = 0;
    }

    // NOTE: (yiliang.siew) We're doing it without concepts/constraints since we still do not compile against C++20 for Unreal at the
    // moment and we need to support that.
    /// Iterator helper for forward iteration over the elements of the hash table. Helps implement STL range functionality.
protected:
    struct SEntry;

public:
    template <bool bConst>
    class Iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = KeyValueType;
        using pointer = typename std::conditional_t<bConst, const KeyValueType*, KeyValueType*>;
        using reference = typename std::conditional_t<bConst, const KeyValueType&, KeyValueType&>;

    private:
        explicit Iterator(SEntry* InEntry, SEntry* InEnd) : _CurrentEntry(InEntry), _End(InEnd)
        {
            EnsureOccupiedOrEnd();
        }

    public:
        // Prefix increment overload.
        ULANG_FORCEINLINE Iterator& operator++()
        {
            if (_CurrentEntry < _End)
            {
                ++_CurrentEntry;
                EnsureOccupiedOrEnd();
            }
            return *this;
        }

        // Postfix increment overload.
        ULANG_FORCEINLINE Iterator operator++(int)
        {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        ULANG_FORCEINLINE bool operator!=(const Iterator& Other) const
        {
            ULANG_ASSERTF(_End == Other._End, "Iterator ends were mismatched!");
            return _CurrentEntry != Other._CurrentEntry;
        }

        ULANG_FORCEINLINE bool operator==(const Iterator& Other) const
        {
            return !(*this != Other);
        }

        template <bool _bConst = bConst>
        ULANG_FORCEINLINE std::enable_if_t<!_bConst, reference> operator*()
        {
            return _CurrentEntry->_KeyValue;
        }

        template <bool _bConst = bConst>
        ULANG_FORCEINLINE std::enable_if_t<_bConst, reference> operator*() const
        {
            return _CurrentEntry->_KeyValue;
        }

        template <bool _bConst = bConst>
        ULANG_FORCEINLINE std::enable_if_t<!_bConst, pointer> operator->()
        {
            return _CurrentEntry->_KeyValue;
        }

        template <bool _bConst = bConst>
        ULANG_FORCEINLINE std::enable_if_t<_bConst, pointer> operator->() const
        {
            return _CurrentEntry->_KeyValue;
        }

    private:
        void EnsureOccupiedOrEnd()
        {
            // If the hash is `0`, it means that the space in the hash table is currently free/unused.
            while (_CurrentEntry < _End && _CurrentEntry->_Hash == 0)
            {
                ++_CurrentEntry;
            }
        }

        SEntry* _CurrentEntry;
        SEntry* _End;

        friend class THashTable;
    };

    ULANG_FORCEINLINE Iterator<false> begin()
    {
        if (_Entries != nullptr && _NumEntries != 0)
        {
            return Iterator<false>{_Entries, _Entries + _NumEntries};
        }
        return end();
    }

    ULANG_FORCEINLINE Iterator<false> end()
    {
        return Iterator<false>{_Entries + _NumEntries, _Entries + _NumEntries};
    }

    ULANG_FORCEINLINE Iterator<true> begin() const
    {
        return cbegin();
    }

    ULANG_FORCEINLINE Iterator<false> end() const
    {
        return cend();
    }

    ULANG_FORCEINLINE Iterator<true> cbegin() const
    {
        if (_Entries != nullptr && _NumEntries != 0)
        {
            return Iterator<true>{_Entries, _Entries + _NumEntries};
        }
        return cend();
    }

    ULANG_FORCEINLINE Iterator<true> cend() const
    {
        return Iterator<true>{_Entries + _NumEntries, _Entries + _NumEntries};
    }

protected:
    // Load factor = what fraction of entries are occupied
    static constexpr uint64_t MaxLoadFactorNumerator = 7;
    static constexpr uint64_t MaxLoadFactorDenominator = 8;

    struct SEntry
    {
        uint32_t  _Hash;  // 0 means unused
        KeyValueType _KeyValue; // Some value, e.g. index into some external array
    };

    // Hash values in the table must not be zero
    ULANG_FORCEINLINE static uint32_t ComputeNonZeroHash(const KeyType& Key)
    {
        uint32_t Hash = HashTraits::GetKeyHash(Key);
        return Hash + uint32_t(Hash == 0);
    }

    // Get desired position for an entry given a hash value
    ULANG_FORCEINLINE uint32_t DesiredPos(uint32_t Hash) const
    {
        return Hash & (_NumEntries - 1);
    }

    // Get probe distance of an entry with given hash and position in entry array
    ULANG_FORCEINLINE uint32_t ProbeDistance(uint32_t Hash, uint32_t Pos) const
    {
        return (Pos + _NumEntries - Hash) & (_NumEntries - 1);
    }

    KeyValueType& InsertInternal(KeyValueType&& KeyValue)
    {
        if ((_NumOccupied + 1) * MaxLoadFactorDenominator >= _NumEntries * MaxLoadFactorNumerator)
        {
            Grow();
        }
        bool bAlreadyExists{};
        const uint32_t NewPos = InsertInternal(ComputeNonZeroHash(KeyValue), Move(KeyValue), &bAlreadyExists);
        if (!bAlreadyExists)
        {
            ++_NumOccupied;
        }
        return _Entries[NewPos]._KeyValue;
    }

    // Create a new entry
    // Use Robin Hood mechanism to rearrange entries to minimize probe distance
    // Returns position of new entry
    uint32_t InsertInternal(uint32_t Hash, KeyValueType&& Value, bool* bAlreadyExists)
    {
        uint32_t Pos = DesiredPos(Hash);
        uint32_t Distance = 0;
        for (;;)
        {
            SEntry& Entry = _Entries[Pos];
            // If the entry already exists, just return it.
            if (Entry._Hash == Hash)
            {
                const KeyType& Key = Value;    // Make sure we are looking at just the key, not the value
                if (Entry._KeyValue == Key)    // Check for equality of key regardless of value
                {
                    if (bAlreadyExists)
                    {
                        *bAlreadyExists = true;
                    }
                    if (!TAreTypesEqual<KeyType, KeyValueType>::Value)    // Equality could be stored in the template as constexpr bool bIsSet
                    {
                        Entry._KeyValue = Move(Value);    // Update value as it might be different
                    }
                    return Pos;
                }
            }

            if (Entry._Hash == 0)
            {
                Entry._Hash = Hash;
                new (&Entry._KeyValue) KeyValueType(Move(Value));
                return Pos;
            }

            // If the existing element has a shorter probe distance, swap places and keep going
            // I.e. we are maintaining the following invariant:
            // Given a hash, all entries from its desired pos to the last entry with that hash have a larger or equal probe distance than their probe distance with regard to the hash
            uint32_t ExistingDistance = ProbeDistance(Entry._Hash, Pos);
            if (ExistingDistance < Distance)
            {
                uLang::Swap(Entry._Hash, Hash);
                uLang::Swap(Entry._KeyValue, Value);
                Distance = ExistingDistance;
            }

            Pos = DesiredPos(Pos + 1);
            ++Distance;
        }
    }

    // Look up a key, return its index
    ULANG_FORCEINLINE uint32_t Lookup(const KeyType& Key) const
    {
        if (!_NumEntries)
        {
            return uint32_t(IndexNone);
        }

        uint32_t Hash = ComputeNonZeroHash(Key);
        uint32_t Pos = DesiredPos(Hash);
        uint32_t Distance = 0;
        for (;;)
        {
            const SEntry& Entry = _Entries[Pos];
            // We know that when we probe an element during insertion, the one with the longer probe distance of the two gets to keep the slot. 
            // So if we're looking for an element that exists, we should expect to never see an existing element with a shorter probe distance 
            // than our current distance (if that had happened, there would have been a swap during insertion!).
            if (Entry._Hash == 0 || Distance > ProbeDistance(Entry._Hash, Pos))
            {
                return uint32_t(IndexNone);
            }
            // 32-bit hashes, if computed right, are pretty darn unique, so the chance that the hash matches but the key doesn't is tiny (2^-32)
            // That means that if the hash matches, the key will practically always match as well
            // So we really only ever have to do at most one real key comparison per lookup
            if (Entry._Hash == Hash && Entry._KeyValue == Key)
            {
                return Pos;
            }

            Pos = (Pos + 1) & (_NumEntries - 1);
            ++Distance;
        }
    }

    // Allocate memory according to currently set _NumEntries
    void Allocate()
    {
        if (_NumEntries)
        {
            ULANG_ASSERTF(CMath::IsPowerOf2(_NumEntries), "_NumEntries must be a power of 2.");

            size_t BytesToAllocate = _NumEntries * sizeof(SEntry);
            _Entries = (SEntry*)_Allocator.Allocate(BytesToAllocate);
            for (uint32_t Pos = 0; Pos < _NumEntries; ++Pos)
            {
                _Entries[Pos]._Hash = 0;
            }
        }
    }

    // Double the size of the table
    void Grow()
    {
        SEntry* PrevEntries = _Entries;
        uint32_t PrevNumEntries = _NumEntries;
        _NumEntries = PrevNumEntries ? _NumEntries * 2 : 4;
        Allocate();
        if (PrevNumEntries)
        {
            for (uint32_t Pos = 0; Pos < PrevNumEntries; ++Pos)
            {
                SEntry& PrevEntry = PrevEntries[Pos];
                uint32_t Hash = PrevEntry._Hash;
                if (Hash)
                {
                    InsertInternal(Hash, Move(PrevEntry._KeyValue), nullptr);
                    PrevEntry._KeyValue.~KeyValueType();
                }
            }
            _Allocator.Deallocate(PrevEntries);
        }
    }

    SEntry*  _Entries{};
    uint32_t _NumEntries{};   // How many entries we have allocated in total
    uint32_t _NumOccupied{};  // How many entries are actually occupied

    /// How to allocate the memory
    /// This allocator can be 0 in size
    AllocatorType _Allocator;
};
}
