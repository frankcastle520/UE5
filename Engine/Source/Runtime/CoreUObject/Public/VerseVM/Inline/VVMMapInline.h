// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Async/ExternalMutex.h"
#include "Async/UniqueLock.h"
#include "VerseVM/Inline/VVMEqualInline.h"
#include "VerseVM/VVMMap.h"
#include "VerseVM/VVMNativeAllocationGuard.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{

inline void VMapBase::Add(FAllocationContext Context, VValue Key, VValue Value)
{
	check(Capacity > 0);
	UE::FExternalMutex ExternalMutex(Mutex);
	UE::TUniqueLock Lock(ExternalMutex);

	uint32 KeyHash = GetTypeHash(Key);
	AddWithoutLocking(Context, KeyHash, Key, Value);
}

inline void VMapBase::AddTransactionally(FAllocationContext Context, VValue Key, VValue Value)
{
	check(Capacity > 0);
	UE::FExternalMutex ExternalMutex(Mutex);
	UE::TUniqueLock Lock(ExternalMutex);

	uint32 KeyHash = GetTypeHash(Key);
	bool bTransactional = true;
	AddWithoutLocking(Context, KeyHash, Key, Value, bTransactional);
}

inline VMapBase::VMapBase(FAllocationContext Context, uint32 InitialCapacity, VEmergentType* Type)
	: VHeapValue(Context, Type)
	, NumElements(0)
	, Capacity(0)
{
	SetIsDeeplyMutable();
	Reserve(Context, InitialCapacity);
}

template <typename GetEntryByIndex>
inline VMapBase::VMapBase(FAllocationContext Context, uint32 MaxNumEntries, const GetEntryByIndex& GetEntry, VEmergentType* Type)
	: VHeapValue(Context, Type)
	, NumElements(0)
	, Capacity(0)
{
	SetIsDeeplyMutable();
	Reserve(Context, MaxNumEntries * 2);

	// Constructing a map in Verse has these semantics:
	// - If the same key appears more than once, it's as if only the last key was provided.
	// - The order of the map is based on the textual order a map is written in.
	// - E.g, map{K1=>V1, K2=>V2} has the order (K1, V1) then (K2, V2).
	//   And map{K1=>V1, K2=>V2, K1=>V3} has the order (K2, V2) then (K1, V3).
	for (uint32 Index = 0; Index < MaxNumEntries; ++Index)
	{
		TPair<VValue, VValue> Pair = GetEntry(Index);
		uint32 KeyHash = GetTypeHash(Pair.Key);
		TPair<uint32, bool> Res = AddWithoutLocking(Context, KeyHash, Pair.Key, Pair.Value); // We don't need to lock because we can't be visited by the GC until after the next handshake.
		uint32 Slot = Res.Get<0>();
		bool SlotOverwritten = Res.Get<1>();
		if (SlotOverwritten)
		{
			SequenceType* SequenceTable = GetSequenceTable();
			uint32 SeqIdx = 0;
			while (SeqIdx < NumElements)
			{
				if (SequenceTable[SeqIdx] == Slot)
				{
					// if we've overwritten a value, we need to change the sequence table so the new slot is at the end.
					FMemory::Memmove(SequenceTable + SeqIdx, SequenceTable + SeqIdx + 1, sizeof(SequenceTable[0]) * (NumElements - SeqIdx - 1));
					SequenceTable[NumElements - 1] = Slot;
					break;
				}
				++SeqIdx;
			}
		}
	}
}

template <typename MapType>
inline void VMapBase::Serialize(MapType*& This, FAllocationContext Context, FAbstractVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		uint64 NumElements = 0;
		Visitor.BeginArray(TEXT("Values"), NumElements);
		This = &VMapBase::New<MapType>(Context, (uint32)NumElements).template StaticCast<MapType>();
		for (uint64 I = (uint32)NumElements; I != 0; --I)
		{
			VValue Key, Value;
			Visitor.VisitPair([&Visitor, &Key, &Value] {
				Visitor.Visit(Key, TEXT("Key"));
				Visitor.Visit(Value, TEXT("Value"));
			});
			This->Add(Context, Key, Value);
		}
		Visitor.EndArray();
	}
	else
	{
		uint64 NumElements = (uint64)This->NumElements;
		Visitor.BeginMap(TEXT("Values"), NumElements);
		for (auto MapIt : *This)
		{
			Visitor.VisitPair([&Visitor, &MapIt] {
				Visitor.Visit(MapIt.Key, TEXT("Key"));
				Visitor.Visit(MapIt.Value, TEXT("Value"));
			});
		}
		Visitor.EndMap();
	}
}

template <typename MapType, typename GetEntryByIndex>
inline VMapBase& VMapBase::New(FAllocationContext Context, uint32 MaxNumEntries, const GetEntryByIndex& GetEntry)
{
	return *new (FAllocationContext(Context).AllocateFastCell(sizeof(VMapBase))) VMapBase(Context, MaxNumEntries, GetEntry, &MapType::GlobalTrivialEmergentType.Get(Context));
}

} // namespace Verse

#endif
