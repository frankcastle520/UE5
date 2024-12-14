// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMTransaction.h"

namespace Verse
{

void FTransactionLog::FEntry::MarkReferencedCells(FMarkStack& MarkStack)
{
	if (Owner.Is<VCell*>())
	{
		MarkStack.MarkNonNull(Owner.As<VCell*>());
	}
	else if (Owner.Is<UObject*>())
	{
		MarkStack.MarkNonNull(Owner.As<UObject*>());
	}
	else if (Owner.Is<TAux<void>>())
	{
		MarkStack.MarkAuxNonNull(Owner.As<TAux<void>>().GetPtr());
	}
	else
	{
		VERSE_UNREACHABLE();
	}

	if (Slot.Is<TWriteBarrier<TAux<void>>*>())
	{
		MarkStack.MarkAux(BitCast<void*>(OldValue));
	}
	else
	{
		if (VCell* Cell = VValue::Decode(OldValue).ExtractCell())
		{
			MarkStack.MarkNonNull(Cell);
		}
	}
}

void FTransactionLog::MarkReferencedCells(FMarkStack& MarkStack)
{
	for (FEntry& Entry : Log)
	{
		Entry.MarkReferencedCells(MarkStack);
	}

	for (FAuxOrCell Root : Roots)
	{
		if (Root.Is<VCell*>())
		{
			MarkStack.MarkNonNull(Root.As<VCell*>());
		}
		else if (Root.Is<UObject*>())
		{
			MarkStack.MarkNonNull(Root.As<UObject*>());
		}
		else if (Root.Is<TAux<void>>())
		{
			MarkStack.MarkAuxNonNull(Root.As<TAux<void>>().GetPtr());
		}
		else
		{
			VERSE_UNREACHABLE();
		}
	}
}

// TODO: We should treat the owner as a weak reference and only mark the old value
// if the owner is marked. However, to do that, we also need to make sure we can prune
// dead entries from the log during census, which runs concurrent to the mutator.
// Therefore, we need a concurrent algorithm for this. For now, since it's abundantly
// likely that the "var" cell is alive when used in the middle of a transaction,
// we just treat it as a root.
void FTransaction::MarkReferencedCells(FTransaction& _Transaction, FMarkStack& MarkStack)
{
	for (FTransaction* Transaction = &_Transaction; Transaction; Transaction = Transaction->Parent)
	{
		Transaction->Log.MarkReferencedCells(MarkStack);
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
