// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "AutoRTFM/AutoRTFM.h"
#include "VVMCell.h"
#include "VVMContext.h"
#include "VVMLog.h"
#include "VVMWriteBarrier.h"
#include <Containers/Array.h>
#include <Containers/Set.h>
#include <bit>

namespace Verse
{

struct FMarkStack;

template <typename T, typename... Ts>
struct TagOf;

template <typename T, typename... Ts>
struct TagOf<T, T, Ts...>
{
	static constexpr uintptr_t Value = 0;
};

template <typename T, typename U, typename... Ts>
struct TagOf<T, U, Ts...>
{
	static constexpr uintptr_t Value = 1 + TagOf<T, Ts...>::Value;
};

// Ts... are types which are ultimately pointers.
// We tag which of Ts... the variant holds by tagging the lower bits, so the pointers
// must be at least log(sizeof(Ts)...) + 1 byte aligned.
template <typename... Ts>
struct TPtrVariant
{
	static_assert(((sizeof(Ts) == sizeof(uintptr_t)) && ...));

	static constexpr uintptr_t Mask = std::bit_ceil(sizeof...(Ts)) - 1;

	template <typename T>
	TPtrVariant(T InT)
	{
		uintptr_t IncomingPtr = BitCast<uintptr_t>(InT);
		uintptr_t TTag = TagOf<T, Ts...>::Value;
		checkSlow(!(IncomingPtr & Mask));
		Ptr = IncomingPtr | TTag;
	}

	template <typename T>
	bool Is()
	{
		static_assert((std::is_same_v<T, Ts> || ...));
		return (Ptr & Mask) == TagOf<T, Ts...>::Value;
	}

	template <typename T>
	T As()
	{
		static_assert((std::is_same_v<T, Ts> || ...));
		checkSlow(Is<T>());
		return BitCast<T>(Ptr & ~Mask);
	}

	bool operator==(TPtrVariant Other) const
	{
		return Ptr == Other.Ptr;
	}

	uintptr_t RawPtr() const { return Ptr; }

private:
	uintptr_t Ptr;
};

template <typename... Ts>
inline uint32 GetTypeHash(TPtrVariant<Ts...> Ptr)
{
	return PointerHash(BitCast<void*>(Ptr.RawPtr()));
}

using FAuxOrCell = TPtrVariant<VCell*, UObject*, TAux<void>>;

struct FTransactionLog
{
public:
	struct FEntry
	{
		uintptr_t Key() { return Slot.RawPtr(); }

		using FSlot = TPtrVariant<TWriteBarrier<VValue>*, TWriteBarrier<TAux<void>>*>;

		FAuxOrCell Owner; // The object that needs to remain alive so that we can write OldValue into Slot on abort.
		FSlot Slot;       // The memory location we write OldValue to into on abort.
		uint64 OldValue;  // VValue or TAux<void> depending on how Slot is encoded.
		static_assert(sizeof(OldValue) == sizeof(VValue));
		static_assert(sizeof(OldValue) == sizeof(TAux<void>));

		FEntry(FAuxOrCell Owner, TWriteBarrier<VValue>& InSlot, VValue OldValue)
			: Owner(Owner)
			, Slot(&InSlot)
			, OldValue(OldValue.GetEncodedBits())
		{
		}

		FEntry(FAuxOrCell Owner, TWriteBarrier<TAux<void>>& InSlot, TAux<void> OldValue)
			: Owner(Owner)
			, Slot(&InSlot)
			, OldValue(BitCast<uint64>(OldValue.GetPtr()))
		{
		}

		void Abort(FAccessContext Context)
		{
			if (Slot.Is<TWriteBarrier<TAux<void>>*>())
			{
				TWriteBarrier<TAux<void>>* AuxSlot = Slot.As<TWriteBarrier<TAux<void>>*>();
				AuxSlot->Set(Context, TAux<void>(BitCast<void*>(OldValue)));
			}
			else
			{
				TWriteBarrier<VValue>* ValueSlot = Slot.As<TWriteBarrier<VValue>*>();
				ValueSlot->Set(Context, VValue::Decode(OldValue));
			}
		}

		void MarkReferencedCells(FMarkStack&);
	};

	TSet<uintptr_t> IsInLog; // TODO: We should probably use something like AutoRTFM's HitSet
	TSet<FAuxOrCell> Roots;
	TArray<FEntry> Log;

public:
	void Add(FEntry Entry)
	{
		bool AlreadyHasEntry;
		IsInLog.FindOrAdd(Entry.Key(), &AlreadyHasEntry);
		if (!AlreadyHasEntry)
		{
			Log.Add(MoveTemp(Entry));
		}
	}

	// This version avoids loading from Slot until we need it.
	template <typename T>
	void AddImpl(FAuxOrCell Owner, TWriteBarrier<T>& Slot)
	{
		bool AlreadyHasEntry;
		IsInLog.FindOrAdd(FEntry::FSlot(&Slot).RawPtr(), &AlreadyHasEntry);
		if (!AlreadyHasEntry)
		{
			Log.Add(FEntry{Owner, Slot, Slot.Get()});
		}
	}

	template <typename T>
	void Add(VCell* Owner, TWriteBarrier<T>& Slot)
	{
		AddImpl(FAuxOrCell(Owner), Slot);
	}

	void Add(UObject* Owner, TWriteBarrier<VValue>& Slot)
	{
		AddImpl(FAuxOrCell(Owner), Slot);
	}

	template <typename T>
	void Add(TAux<T> Owner, TWriteBarrier<VValue>& Slot)
	{
		AddImpl(FAuxOrCell(BitCast<TAux<void>>(Owner)), Slot);
	}

	void AddRoot(FAuxOrCell Root)
	{
		Roots.Add(Root);
	}

	void Join(FTransactionLog& Child)
	{
		for (FEntry Entry : Child.Log)
		{
			Add(MoveTemp(Entry));
		}
	}

	void Abort(FAccessContext Context)
	{
		for (FEntry Entry : Log)
		{
			Entry.Abort(Context);
		}
	}

	void MarkReferencedCells(FMarkStack&);
};

struct FTransaction
{
	FTransactionLog Log;
	FTransaction* Parent{nullptr};
	bool bHasStarted{false};
	bool bHasCommitted{false};
	bool bHasAborted{false};

	// Note: We can Abort before we Start because of how leniency works. For example, we can't
	// Start the transaction until the effect token is concrete, but the effect token may become
	// concrete after failure occurs.
	void Start(FRunningContext Context)
	{
		V_DIE_IF(bHasCommitted);
		V_DIE_IF(bHasStarted);
		V_DIE_IF(Parent);
		bHasStarted = true;

		if (!bHasAborted)
		{
			AutoRTFM::ForTheRuntime::StartTransaction();
			Parent = Context.CurrentTransaction();
			Context.SetCurrentTransaction(this);
		}
	}

	// We can't call Commit before we Start because we serialize Start then Commit via the effect token.
	void Commit(FRunningContext Context)
	{
		V_DIE_UNLESS(bHasStarted);
		V_DIE_IF(bHasAborted);
		V_DIE_IF(bHasCommitted);
		bHasCommitted = true;
		AutoRTFM::ForTheRuntime::CommitTransaction();
		if (Parent)
		{
			Parent->Log.Join(Log);
		}
		Context.SetCurrentTransaction(Parent);
	}

	// See above comment as to why we might Abort before we start.
	void Abort(FRunningContext Context)
	{
		V_DIE_IF(bHasCommitted);
		V_DIE_IF(bHasAborted);
		bHasAborted = true;
		if (bHasStarted)
		{
			V_DIE_UNLESS(Context.CurrentTransaction() == this);
			AutoRTFM::AbortTransaction();
			AutoRTFM::ForTheRuntime::ClearTransactionStatus();
			Log.Abort(Context);
			Context.SetCurrentTransaction(Parent);
		}
		else
		{
			V_DIE_IF(Parent);
		}
	}

	template <typename T>
	void LogBeforeWrite(FAccessContext Context, VCell* Owner, TWriteBarrier<T>& Slot)
	{
		Log.Add(Owner, Slot);
	}

	void LogBeforeWrite(FAccessContext Context, UObject* Owner, TWriteBarrier<VValue>& Slot)
	{
		Log.Add(Owner, Slot);
	}

	template <typename T>
	void LogBeforeWrite(FAccessContext Context, TAux<T> Owner, TWriteBarrier<VValue>& Slot)
	{
		Log.Add(Owner, Slot);
	}

	void AddRoot(FAccessContext Context, VCell* Root)
	{
		Log.AddRoot(FAuxOrCell(Root));
	}

	void AddRoot(FAccessContext Context, UObject* Root)
	{
		Log.AddRoot(FAuxOrCell(Root));
	}

	template <typename T>
	void AddAuxRoot(FAccessContext Context, TAux<T> Root)
	{
		Log.AddRoot(FAuxOrCell(BitCast<TAux<void>>(Root)));
	}

	static void MarkReferencedCells(FTransaction&, FMarkStack&);
};

} // namespace Verse
#endif // WITH_VERSE_VM
