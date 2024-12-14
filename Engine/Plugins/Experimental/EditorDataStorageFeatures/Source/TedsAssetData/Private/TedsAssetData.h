// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Containers/StringFwd.h"
#include "Elements/Common/TypedElementHandles.h"

struct FAssetData;

class IEditorDataStorageProvider;

namespace UE::Editor::AssetData::Private
{
	/**
	 * Manage the registration and life cycle of the row related representing the data from the asset registry into TEDS.
	 */
	class FTedsAssetData
	{
	public:
		FTedsAssetData(IEditorDataStorageProvider& InDatabase);

		~FTedsAssetData();

		void ProcessAllEvents();

	private:
		void OnAssetsAdded(TConstArrayView<FAssetData> InAssetsAdded);
		void OnAssetsRemoved(TConstArrayView<FAssetData> InAssetsRemoved);
		void OnAssetsUpdated(TConstArrayView<FAssetData> InAssetsUpdated);
		void OnAssetsUpdatedOnDisk(TConstArrayView<FAssetData> InAssetsUpdated);

		void OnAssetRenamed(const FAssetData& InAsset, const FString& InOldObjectPath);

		void OnPathsAdded(TConstArrayView<FStringView> InPathsAdded);
		void OnPathsRemoved(TConstArrayView<FStringView> InPathsRemoved);

		IEditorDataStorageProvider& Database;
		DataStorage::TableHandle PathsTable = DataStorage::InvalidTableHandle;
		DataStorage::TableHandle AssetsDataTable = DataStorage::InvalidTableHandle;

		DataStorage::QueryHandle UpdateAssetsInPathQuery = DataStorage::InvalidQueryHandle;
		DataStorage::QueryHandle UpdateAssetsInPath = DataStorage::InvalidQueryHandle;
		DataStorage::QueryHandle ResolveMissingAssetInPathQuery = DataStorage::InvalidQueryHandle;
		DataStorage::QueryHandle UpdateParentToChildrenAssetPathQuery = DataStorage::InvalidQueryHandle;
		DataStorage::QueryHandle ResolveMissingParentPathQuery = DataStorage::InvalidQueryHandle;
		DataStorage::QueryHandle RemoveUpdatedPathTagQuery = DataStorage::InvalidQueryHandle;
		DataStorage::QueryHandle RemoveUpdatedAssetDataTagQuery = DataStorage::InvalidQueryHandle;
	};
} // namespace UE::Editor::AssetData::Private
