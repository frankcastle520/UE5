// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementDatabaseIndexTable.h"

namespace UE::Editor::DataStorage
{
	RowHandle FIndexTable::FindIndexedRow(
		EGlobalLockScope LockScope, IndexHash Index) const
	{	
		FScopedSharedLock Lock(LockScope);
	
		const RowHandle* Result = IndexLookupMap.Find(Index);
		return Result ? *Result : InvalidRowHandle;
	}

	void FIndexTable::BatchIndexRows(EGlobalLockScope LockScope,
		TConstArrayView<TPair<IndexHash, RowHandle>> IndexRowPairs)
	{
		FScopedExclusiveLock Lock(LockScope);
	
		IndexLookupMap.Reserve(IndexLookupMap.Num() + IndexRowPairs.Num());
		ReverseIndexLookupMap.Reserve(ReverseIndexLookupMap.Num() + IndexRowPairs.Num());

		for (const TPair<IndexHash, RowHandle>& IndexAndRow : IndexRowPairs)
		{
			IndexRowUnguarded(IndexAndRow.Key, IndexAndRow.Value);
		}
	}

	void FIndexTable::IndexRow(EGlobalLockScope LockScope,
		IndexHash Index, RowHandle Row)
	{

		FScopedExclusiveLock Lock(LockScope);
		IndexRowUnguarded(Index, Row);
	}

	void FIndexTable::ReindexRow(EGlobalLockScope LockScope,
		IndexHash OriginalIndex, IndexHash NewIndex, 
		RowHandle Row)
	{
		FScopedExclusiveLock Lock(LockScope);
	
		RemoveIndexUnguarded(OriginalIndex);
		IndexRowUnguarded(NewIndex, Row);
	}

	void FIndexTable::RemoveIndex(EGlobalLockScope LockScope, IndexHash Index)
	{
		FScopedExclusiveLock Lock(LockScope);
		RemoveIndexUnguarded(Index);
	}

	void FIndexTable::RemoveRow(EGlobalLockScope LockScope, RowHandle Row)
	{
		FScopedExclusiveLock Lock(LockScope);
	
		if (TMultiMap<RowHandle, IndexHash>::TKeyIterator It = ReverseIndexLookupMap.CreateKeyIterator(Row); It)
		{
			do
			{
				IndexLookupMap.Remove(It.Value());
				++It;
			} while (It);
			ReverseIndexLookupMap.Remove(Row);
		}
	}

	void FIndexTable::IndexRowUnguarded(IndexHash Index, RowHandle Row)
	{
		IndexLookupMap.Add(Index, Row);
		ReverseIndexLookupMap.Add(Row, Index);
	}


	void FIndexTable::RemoveIndexUnguarded(IndexHash Index)
	{
		if (const RowHandle* Row = IndexLookupMap.Find(Index))
		{
			IndexLookupMap.Remove(Index);
			ReverseIndexLookupMap.Remove(*Row, Index);
		}
	}
} // namespace UE::Editor::DataStorage
