// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Templates/Function.h"
#include "Utils.h"

namespace AutoRTFM
{

template<typename T> class TBackwards final
{
	T& Obj;
public:
	TBackwards(T& Obj) : Obj(Obj) {}
	auto begin() { return Obj.rbegin(); }
	auto end() { return Obj.rend(); }
};

template<typename InKeyType, typename InValType>
struct TTaskArrayKeyValuePair
{
	InKeyType Key;
	InValType Val;

	TTaskArrayKeyValuePair() = default;
	TTaskArrayKeyValuePair(const InKeyType& Key, const InValType& Val) : Key(Key), Val(Val){}
	TTaskArrayKeyValuePair(InKeyType&& Key, InValType&& Val) : Key(MoveTemp(Key)), Val(MoveTemp(Val)){}
};

template<typename InElementType>
class TTaskArray
{
	using SKey = const void*;
	using SKeyValuePair = TTaskArrayKeyValuePair<SKey, InElementType>;
	using FInternalArray = TArray<SKeyValuePair>;
public:

	TTaskArray() = default;
	TTaskArray(const TTaskArray&) = delete;
	void operator=(const TTaskArray&) = delete;

    bool IsEmpty() const { return Latest.IsEmpty() && Stash.IsEmpty(); }

    void Add(InElementType&& Value)
    {
		Latest.Push(TTaskArrayKeyValuePair(SKey{}, MoveTemp(Value)));
    }

	void Add(const InElementType& Value)
	{
		Latest.Push(TTaskArrayKeyValuePair(SKey{}, Value));
	}

	void AddKeyed(SKey Key, InElementType&& Value)
    {
		ASSERT(Key != SKey{});
		Latest.Push(TTaskArrayKeyValuePair(Key, MoveTemp(Value)));
    }

	void AddKeyed(SKey Key, const InElementType& Value)
    {
		ASSERT(Key != SKey{});
		Latest.Push(TTaskArrayKeyValuePair(Key, Value));
    }

	bool DeleteKey(SKey Key)
	{
		ASSERT(Key != SKey{});

		auto EraseLastAddedKeyFromArray = [Key](FInternalArray& Array) -> bool
		{
			typename FInternalArray::SizeType LastIdx = Array.FindLastByPredicate([Key](const SKeyValuePair& Pair)
			{
				return Pair.Key == Key;
			});

			if (LastIdx == INDEX_NONE)
			{
				return false;
			}

			// We found our key to erase! Nuke it.
			Array.RemoveAt(LastIdx);
			return true;
		};

		if (EraseLastAddedKeyFromArray(Latest))
		{
			return true;
		}

		for (FInternalArray& StashedVectorBox : TBackwards(Stash))
		{
			if (EraseLastAddedKeyFromArray(StashedVectorBox))
			{
				return true;
			}
		}

		return false;
	}

	bool DeleteAllMatchingKeys(SKey Key)
	{
		ASSERT(Key != SKey{});

		auto EraseKeyFromArray = [Key](FInternalArray& Array) -> size_t
		{
			return Array.RemoveAll([Key](const SKeyValuePair& Pair)
			{
				return Pair.Key == Key;
			});
		};

		size_t NumErased = EraseKeyFromArray(Latest);

		for (FInternalArray& StashedVectorBox : Stash)
		{
			NumErased += EraseKeyFromArray(StashedVectorBox);
		}

		return NumErased > 0;
	}

    void AddAll(TTaskArray&& Other)
    {
        Canonicalize();

        for (FInternalArray& StashedVectorBox : Other.Stash)
        {
            Stash.Push(MoveTemp(StashedVectorBox));
        }

        Other.Stash.Empty();

        if (!Other.Latest.IsEmpty())
        {
            Stash.Push(MoveTemp(Other.Latest));
			Other.Latest = FInternalArray{};
        }
    }

    template<typename TFunc>
    bool ForEachForward(const TFunc& Func) const
    {
        for (FInternalArray& StashedVectorBox : Stash)
        {
            for (const SKeyValuePair& EntryKVP : StashedVectorBox)
            {
                if (!Func(EntryKVP.Val))
                {
                    return false;
                }
            }
        }

        for (const SKeyValuePair& EntryKVP : Latest)
        {
            if (!Func(EntryKVP.Val))
            {
                return false;
            }
        }
        return true;
    }

    template<typename TFunc>
    bool ForEachForward(const TFunc& Func)
    {
        for (FInternalArray& StashedVectorBox : Stash)
        {
            for (SKeyValuePair& EntryKVP : StashedVectorBox)
            {
                if (!Func(EntryKVP.Val))
                {
                    return false;
                }
            }
        }

        for (SKeyValuePair& EntryKVP : Latest)
        {
            if (!Func(EntryKVP.Val))
            {
                return false;
            }
        }
        return true;
    }

    template<typename TFunc>
    bool ForEachBackward(const TFunc& Func) const
    {
		for (const SKeyValuePair& EntryKVP : TBackwards(Latest))
		{
			if (!Func(EntryKVP.Val))
			{
				return false;
			}
		}

		for (const FInternalArray& StashedVectorBox : TBackwards(Stash))
		{
			for (const SKeyValuePair& EntryKVP : TBackwards(StashedVectorBox))
			{
				if (!Func(EntryKVP.Val))
				{
					return false;
				}
			}
		}

        return true;
    }

	template<typename TFunc>
	bool ForEachBackward(const TFunc& Func)
	{
		for (SKeyValuePair& EntryKVP : TBackwards(Latest))
		{
			if (!Func(EntryKVP.Val))
			{
				return false;
			}
		}

		for (FInternalArray& StashedVectorBox : TBackwards(Stash))
		{
			for (SKeyValuePair& EntryKVP : TBackwards(StashedVectorBox))
			{
				if (!Func(EntryKVP.Val))
				{
					return false;
				}
			}
		}

		return true;
	}

    void Reset()
    {
        Latest.Empty();
        Stash.Empty();
    }

    size_t Num() const
    {
		typename FInternalArray::SizeType Result = Latest.Num();

        for (typename FInternalArray::SizeType Index = 0; Index < Stash.Num(); Index++)
        {
            const FInternalArray& StashedVector = Stash[Index];
            Result += StashedVector.Num();
        }

		return static_cast<size_t>(Result);
    }

private:
    // We don't want to do this too often; currently we just do it where it's asymptitically relevant like AddAll. This doesn't
    // logically change the TaskArray but it changes its internal representation. Hence the use of `mutable` and hence why this
    // method is `const`.
    void Canonicalize() const
    {
        if (!Latest.IsEmpty())
        {
			Stash.Push(MoveTemp(Latest));
			Latest = FInternalArray{};
		}
    }
    
    mutable FInternalArray Latest;
    mutable TArray<FInternalArray> Stash;
};

} // namespace AutoRTFM
