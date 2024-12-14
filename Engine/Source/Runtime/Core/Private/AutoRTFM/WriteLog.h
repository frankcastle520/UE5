// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/HitSet.h"
#include "Utils.h"
#include <stdint.h>

namespace AutoRTFM
{
	using FMemoryLocation = FHitSet::Key;

    class FWriteLogEntry final
    {
		static constexpr uintptr_t IsSmallBit = 0x8000;

        FMemoryLocation OriginalAndIsSmallAndSize;
        uintptr_t Copy;
	public:

        UE_AUTORTFM_FORCEINLINE FWriteLogEntry() = default;
        UE_AUTORTFM_FORCEINLINE FWriteLogEntry(const FWriteLogEntry&) = default;
        UE_AUTORTFM_FORCEINLINE FWriteLogEntry& operator=(const FWriteLogEntry&) = default;

        UE_AUTORTFM_FORCEINLINE explicit FWriteLogEntry(void* Original, size_t Size, void* Copy) :
			OriginalAndIsSmallAndSize(Original), Copy(reinterpret_cast<uintptr_t>(Copy))
        {
			OriginalAndIsSmallAndSize.SetTopTag(static_cast<uint16_t>(Size));
        }

		template<unsigned int SIZE> UE_AUTORTFM_FORCEINLINE static FWriteLogEntry CreateSmall(void* Original)
		{
			static_assert(SIZE <= 8);

			FWriteLogEntry Entry;

			Entry.OriginalAndIsSmallAndSize = Original;

			// We store the 8-byte or less write log entry in the write log itself since it is small.
			uintptr_t Temp = 0;
			memcpy(&Temp, Original, SIZE);
			Entry.Copy = Temp;

			size_t Size = IsSmallBit | SIZE;

			ASSERT(Size <= UINT16_MAX);

			Entry.OriginalAndIsSmallAndSize.SetTopTag(static_cast<uint16_t>(Size));

			return Entry;
		}

		UE_AUTORTFM_FORCEINLINE void* GetOriginal()
		{
			return OriginalAndIsSmallAndSize.Get();
		}

		UE_AUTORTFM_FORCEINLINE void* GetCopy()
		{
			if (IsSmall())
			{
				return reinterpret_cast<void*>(&Copy);
			}
			else
			{
				return reinterpret_cast<void*>(Copy);
			}
		}

		UE_AUTORTFM_FORCEINLINE size_t GetSize() const
		{
			return OriginalAndIsSmallAndSize.GetTopTag() & ~IsSmallBit;
		}

		UE_AUTORTFM_FORCEINLINE bool IsSmall() const
		{
			return OriginalAndIsSmallAndSize.GetTopTag() & IsSmallBit;
		}
    };

    class FWriteLog final
    {
        struct FWriteLogEntryBucket final
        {
            static constexpr uint32_t BucketSize = 128;

			// Specify a constructor so that `Entries` below will be left uninitialized.
			explicit FWriteLogEntryBucket() {}

            FWriteLogEntry Entries[BucketSize];
            size_t Size = 0;
            FWriteLogEntryBucket* Next = nullptr;
            FWriteLogEntryBucket* Prev = nullptr;
        };

    public:
        void Push(FWriteLogEntry Entry)
        {
            if (nullptr == Start)
            {
                Start = new FWriteLogEntryBucket();
                Current = Start;
            }

            if (FWriteLogEntryBucket::BucketSize == Current->Size)
            {
                Current->Next = new FWriteLogEntryBucket();
				Current->Next->Prev = Current;
                Current = Current->Next;
            }

            Current->Entries[Current->Size++] = Entry;

            TotalSize++;
            return;
        }

        ~FWriteLog()
        {
            Reset();
        }

        struct Iterator final
        {
            explicit Iterator(FWriteLogEntryBucket* const Bucket) : Bucket(Bucket) {}

            FWriteLogEntry& operator*()
            {
                return Bucket->Entries[Offset];
            }

            void operator++()
            {
                Offset++;

                if (Offset == Bucket->Size)
                {
                    Bucket = Bucket->Next;
                    Offset = 0;
                }
            }

            bool operator!=(Iterator& Other) const
            {
                return (Other.Bucket != Bucket) || (Other.Offset != Offset);
            }

        private:
            FWriteLogEntryBucket* Bucket;
            size_t Offset = 0;
        };

        struct ReverseIterator final
        {
            explicit ReverseIterator(FWriteLogEntryBucket* const Bucket) : Bucket(Bucket) {}

            FWriteLogEntry& operator*()
            {
                return Bucket->Entries[Bucket->Size - Offset - 1];
            }

            void operator++()
            {
                Offset++;

                if (Offset == Bucket->Size)
                {
                    Bucket = Bucket->Prev;
                    Offset = 0;
                }
            }

            bool operator!=(const ReverseIterator& Other) const
            {
                return (Other.Bucket != Bucket) || (Other.Offset != Offset);
            }

        private:
            FWriteLogEntryBucket* Bucket;
            size_t Offset = 0;
        };

        Iterator begin()
        {
            return Iterator(Start);
        }

        ReverseIterator rbegin()
        {
            return ReverseIterator(Current);
        }

        Iterator end()
        {
            return Iterator(nullptr);
        }

        ReverseIterator rend()
        {
            return ReverseIterator(nullptr);
        }

        void Reset()
        {
            while (nullptr != Start)
            {
                FWriteLogEntryBucket* const Old = Start;
                Start = Start->Next;
                delete Old;
            }

            Current = nullptr;
            TotalSize = 0;
        }

        UE_AUTORTFM_FORCEINLINE bool IsEmpty() const { return 0 == TotalSize; }
		UE_AUTORTFM_FORCEINLINE size_t Num() const { return TotalSize; }
    private:

        FWriteLogEntryBucket* Start = nullptr;
        FWriteLogEntryBucket* Current = nullptr;
        size_t TotalSize = 0;
    };
}
