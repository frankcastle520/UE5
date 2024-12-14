// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/NumericLimits.h"
#include "Templates/TypeHash.h"

#include "TypedElementHandles.generated.h"

namespace UE::Editor::DataStorage
{
	using TableHandle = uint64;
	static constexpr TableHandle InvalidTableHandle = TNumericLimits<TableHandle>::Max();

	using RowHandle = uint64;
	static constexpr RowHandle InvalidRowHandle = 0;

	using QueryHandle = uint64;
	static constexpr QueryHandle InvalidQueryHandle = TNumericLimits<QueryHandle>::Max();
} // namespace UE::Editor::DataStorage

/*
 * FTedsRowHandle is a strongly typed wrapper around UE::Editor::DataStorage::RowHandle and should only be used in cases where you need the extra info.
 * E.g for reflection/UHT or for template specializing something that needs to know the semantics of the row handle.
 * For all other cases, you should use the regular typedef UE::Editor::DataStorage::RowHandle
 */
USTRUCT()
struct FTedsRowHandle
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 RowHandle = UE::Editor::DataStorage::InvalidRowHandle;

	operator UE::Editor::DataStorage::RowHandle () const
	{
		return RowHandle;
	}

	FTedsRowHandle& operator=(UE::Editor::DataStorage::RowHandle InRowHandle)
	{
		RowHandle = InRowHandle;
		return *this;
	}
		
	friend uint32 GetTypeHash(const FTedsRowHandle& Key)
	{
		return GetTypeHash(Key.RowHandle);
	}
};

static_assert(sizeof(FTedsRowHandle::RowHandle) == sizeof(UE::Editor::DataStorage::RowHandle), "RowHandle and RowHandle wrapper sizes should match");