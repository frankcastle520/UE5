// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Containers/Map.h"
#include "CoreTypes.h"
#include "Inline/VVMValueInline.h"
#include "VVMCell.h"
#include "VVMContext.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMHeap.h"
#include "VVMValue.h"
#include "VVMWriteBarrier.h"
#include "VerseVM/Inline/VVMValueInline.h"

namespace Verse
{

/*
 * The following types can be used as keys:
 * logic
 * int
 * float
 * char
 * string
 * enum
 * A class, if it’s comparable
 * An option, if the element type is comparable
 * An array, if the element type is comparable
 * A map if both the key and the value types are comparable
 * A tuple if all elements in the tuple are comparable
 */

struct VMapBase : VHeapValue
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VHeapValue);
	using KeyType = TWriteBarrier<VValue>;
	using ValType = TWriteBarrier<VValue>;
	using PairType = TPair<KeyType, ValType>;
	using SequenceType = uint32;

	struct RangedForIterator
	{
		RangedForIterator(const VMapBase* Map, uint32 Index)
			: Map(Map)
			, Index(Index) {}
		TPair<VValue, VValue> operator*() const
		{
			const PairType* PairTable = Map->GetPairTable();
			const SequenceType* SequenceTable = Map->GetSequenceTable();
			return {PairTable[SequenceTable[Index]].Key.Get(), PairTable[SequenceTable[Index]].Value.Get()};
		}
		bool operator==(const RangedForIterator& Rhs) const { return Index == Rhs.Index; }
		bool operator!=(const RangedForIterator& Rhs) const { return Index != Rhs.Index; }
		RangedForIterator& operator++()
		{
			++Index;
			return *this;
		}

		const VMapBase* Map;
		uint32 Index;
	};

protected:
	VMapBase(FAllocationContext Context, uint32 InitialCapacity, VEmergentType* Type);
	template <typename GetEntryByIndex>
	VMapBase(FAllocationContext Context, uint32 MaxNumEntries, const GetEntryByIndex& GetEntry, VEmergentType* Type);

	// This should only be called if you already have the required mutexes or know you don't need them.
	// Returns the slot in the data table where the value was inserted and a boolean indicating if an existing entry was replaced.
	TPair<uint32, bool> AddWithoutLocking(FAllocationContext Context, uint32 KeyHash, VValue Key, VValue Value, bool bTransactional = false);

public:
	uint32 Num() const
	{
		return NumElements;
	}

	VValue FindByHashWithSlot(FAllocationContext Context, uint32 Hash, VValue Key, uint32* OutSlot);
	VValue FindWithSlot(FAllocationContext Context, VValue Key, SequenceType* OutSlot)
	{
		uint32 Hash = GetTypeHash(Key);
		return FindByHashWithSlot(Context, Hash, Key, OutSlot);
	}
	VValue FindByHash(FAllocationContext Context, uint32 Hash, VValue Key)
	{
		SequenceType Slot;
		return FindByHashWithSlot(Context, Hash, Key, &Slot);
	}
	VValue Find(FAllocationContext Context, VValue Key)
	{
		uint32 Hash = GetTypeHash(Key);
		return FindByHash(Context, Hash, Key);
	}

	// GetKey/GetValue doesn't verify that Index is within limits and
	// only works as long as nothing is removed from the map.
	VValue GetKey(uint32 Index)
	{
		check(Index < Capacity);
		PairType* PairTable = GetPairTable();
		SequenceType* SequenceTable = GetSequenceTable();
		return PairTable[SequenceTable[Index]].Key.Follow();
	}

	VValue GetValue(uint32 Index)
	{
		check(Index < Capacity);
		PairType* PairTable = GetPairTable();
		SequenceType* SequenceTable = GetSequenceTable();
		return PairTable[SequenceTable[Index]].Value.Follow();
	}
	void Add(FAllocationContext Context, VValue Key, VValue Value);
	void AddTransactionally(FAllocationContext Context, VValue Key, VValue Value);
	void Reserve(FAllocationContext Context, uint32 InCapacity);

	size_t GetPairTableSizeForCapacity(uint32 InCapacity) const
	{
		return sizeof(PairType) * InCapacity;
	}
	size_t GetSequenceTableSizeForCapacity(uint32 InCapacity) const
	{
		return sizeof(SequenceType) * InCapacity;
	}
	size_t GetPairTableSize() const
	{
		return GetPairTableSizeForCapacity(Capacity);
	}
	size_t GetSequenceTableSize() const
	{
		return GetSequenceTableSizeForCapacity(Capacity);
	}
	size_t GetAllocatedSize() const
	{
		return GetPairTableSize() + GetSequenceTableSize();
	}
	const PairType* GetPairTable() const
	{
		return Data.Get().GetPtr();
	}
	const SequenceType* GetSequenceTable() const
	{
		return SequenceData.Get().GetPtr();
	}
	PairType* GetPairTable()
	{
		return Data.Get().GetPtr();
	}
	SequenceType* GetSequenceTable()
	{
		return SequenceData.Get().GetPtr();
	}

	// These `new` calls are templated so as to avoid boilerplate News/Ctors in VMapBase's subclasses.
	template <typename MapType>
	static VMapBase& New(FAllocationContext Context, uint32 InitialCapacity = 0)
	{
		return *new (FAllocationContext(Context).AllocateFastCell(sizeof(VMapBase))) VMapBase(Context, InitialCapacity, &MapType::GlobalTrivialEmergentType.Get(Context));
	}

	template <typename MapType, typename GetEntryByIndex>
	static VMapBase& New(FAllocationContext Context, uint32 MaxNumEntries, const GetEntryByIndex& GetEntry);

	template <typename MapType>
	static void Serialize(MapType*& This, FAllocationContext Context, FAbstractVisitor& Visitor);

	template <typename MapType, typename TranslationFunc>
	VValue FreezeMeltImpl(FAllocationContext Context, TranslationFunc&& Func);

	COREUOBJECT_API VValue MeltImpl(FAllocationContext Context);

	COREUOBJECT_API bool EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder);

	COREUOBJECT_API uint32 GetTypeHashImpl();
	COREUOBJECT_API void ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter);

	RangedForIterator begin() const
	{
		RangedForIterator it(this, 0);
		return it;
	}

	RangedForIterator end() const
	{
		RangedForIterator it(this, NumElements);
		return it;
	}

	TWriteBarrier<TAux<PairType>> Data;
	TWriteBarrier<TAux<SequenceType>> SequenceData; // initial insert sequence only.  Overwritten values will stay in their original sequence
	uint32 NumElements;
	uint32 Capacity;
};

struct VMap : VMapBase
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VMapBase);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;
	static void SerializeImpl(VMap*& This, FAllocationContext Context, FAbstractVisitor& Visitor) { Super::Serialize<VMap>(This, Context, Visitor); }
};

struct VMutableMap : VMapBase
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VMapBase);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;
	COREUOBJECT_API VValue FreezeImpl(FAllocationContext Context);
	static void SerializeImpl(VMutableMap*& This, FAllocationContext Context, FAbstractVisitor& Visitor) { Super::Serialize<VMutableMap>(This, Context, Visitor); }
};

} // namespace Verse
#endif // WITH_VERSE_VM
