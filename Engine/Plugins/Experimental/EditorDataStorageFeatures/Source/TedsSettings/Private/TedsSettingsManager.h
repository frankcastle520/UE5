// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Templates/SharedPointer.h"

class ISettingsCategory;

class FTedsSettingsManager final : public TSharedFromThis<FTedsSettingsManager>
{
public:

	FTedsSettingsManager();

	const bool IsInitialized() const
	{
		return bIsInitialized;
	}

	void Initialize();
	void Shutdown();

private:

	void RegisterTables(IEditorDataStorageProvider& DataStorage);

	void RegisterQueries(IEditorDataStorageProvider& DataStorage);
	void UnregisterQueries(IEditorDataStorageProvider& DataStorage);

	void RegisterSettings();
	void UnregisterSettings();

	void RegisterSettingsContainer(const FName& ContainerName);

	void UpdateSettingsCategory(TSharedPtr<ISettingsCategory> SettingsCategory, UE::Editor::DataStorage::RowHandle ContainerRow, const bool bQueryExistingRows = true);

	bool bIsInitialized;
	UE::Editor::DataStorage::QueryHandle SelectAllSettingsQuery;
	UE::Editor::DataStorage::TableHandle SettingsContainerTable;
	UE::Editor::DataStorage::TableHandle SettingsCategoryTable;

};
