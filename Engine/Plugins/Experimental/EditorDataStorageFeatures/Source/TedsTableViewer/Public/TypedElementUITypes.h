// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"
#include "Framework/Views/TableViewTypeTraits.h"



/* Template declaration to describe how a row handle behaves as a type for slate widgets like SListView, STreeView etc
 * This allows you to use Row Handles with slate widgets that work on pointers by using the wrapper struct e.g
 * SListView<FTedsRowHandle>
 */
template <>
struct TListTypeTraits<FTedsRowHandle>
{
	typedef FTedsRowHandle NullableType;

	using MapKeyFuncs = TDefaultMapHashableKeyFuncs<FTedsRowHandle, TSharedRef<ITableRow>, false>;
	using MapKeyFuncsSparse = TDefaultMapHashableKeyFuncs<FTedsRowHandle, FSparseItemInfo, false>;
	using SetKeyFuncs = DefaultKeyFuncs<FTedsRowHandle>;

	template<typename U>
	static void AddReferencedObjects(FReferenceCollector&,
		TArray<FTedsRowHandle>&,
		TSet<FTedsRowHandle>&,
		TMap<const U*, FTedsRowHandle>&)
	{
	}

	static bool IsPtrValid(const FTedsRowHandle& InPtr)
	{
		return InPtr != UE::Editor::DataStorage::InvalidRowHandle;
	}

	static void ResetPtr(FTedsRowHandle& InPtr)
	{
		InPtr = UE::Editor::DataStorage::InvalidRowHandle;
	}

	static FTedsRowHandle MakeNullPtr()
	{
		static FTedsRowHandle InvalidRowHandle{ .RowHandle = UE::Editor::DataStorage::InvalidRowHandle };
		return InvalidRowHandle;
	}

	static FTedsRowHandle NullableItemTypeConvertToItemType(const FTedsRowHandle& InPtr)
	{
		return InPtr;
	}

	static FString DebugDump(FTedsRowHandle InPtr)
	{
		return FString::Printf(TEXT("%llu"), InPtr.RowHandle);
	}

	class SerializerType {};
};

// Template declaration to enable using row handles inside of slate widgets like SListView
template <>
struct TIsValidListItem<FTedsRowHandle>
{
	enum
	{
		Value = true
	};
};