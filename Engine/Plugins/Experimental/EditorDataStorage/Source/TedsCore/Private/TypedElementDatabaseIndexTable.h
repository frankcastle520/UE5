// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementQueryStorageInterfaces.h"
#include "GlobalLock.h"


namespace UE::Editor::DataStorage
{
	/**
	 * Storage for an index to row mapping.
	 * Access to the index table is thread safe and guarded by the global lock.
	 */

	class FIndexTable final
	{
	public:
		RowHandle FindIndexedRow(
			EGlobalLockScope LockScope,
			IndexHash Index) const;
		void IndexRow(
			EGlobalLockScope LockScope,
			IndexHash Index, 
			RowHandle Row);
		void BatchIndexRows(
			EGlobalLockScope LockScope,
			TConstArrayView<TPair<IndexHash, RowHandle>> IndexRowPairs);
		void ReindexRow(
			EGlobalLockScope LockScope,
			IndexHash OriginalIndex,
			IndexHash NewIndex,
			RowHandle Row);
		void RemoveIndex(
			EGlobalLockScope LockScope,
			IndexHash Index);
		void RemoveRow(
			EGlobalLockScope LockScope,
			RowHandle Row);

	private:
		TMap<IndexHash, RowHandle> IndexLookupMap;
		TMultiMap<RowHandle, IndexHash> ReverseIndexLookupMap;
	
		void IndexRowUnguarded(IndexHash Index, RowHandle Row);
		void RemoveIndexUnguarded(IndexHash Index);
	};
} // namespace UE::Editor::DataStorage
