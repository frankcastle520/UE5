// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"

#include "RevisionControlProcessors.generated.h"

class IEditorDataStorageProvider;

UCLASS()
class URevisionControlDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~URevisionControlDataStorageFactory() override = default;

	void RegisterTables(IEditorDataStorageProvider& DataStorage) override;
	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

	// Update the overlays for all SCC rows that contain the Column specified
	void UpdateOverlaysForSCCState(IEditorDataStorageProvider* DataStorage, const UScriptStruct* Column) const;

	// Update the color for all actors that currently have an overlay
	void UpdateOverlayColors(IEditorDataStorageProvider* DataStorage) const;


private:
	void RegisterFetchUpdates(IEditorDataStorageProvider& DataStorage);
	void RegisterApplyOverlays(IEditorDataStorageProvider& DataStorage);
	void RegisterRemoveOverlays(IEditorDataStorageProvider& DataStorage);
	void RegisterGeneralQueries(IEditorDataStorageProvider& DataStorage);

	UE::Editor::DataStorage::QueryHandle FetchUpdates = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle ApplyOverlaysObjectToSCC = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle RemoveOverlays = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle UpdateSCCForActors = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle SelectionAdded = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle SelectionRemoved = UE::Editor::DataStorage::InvalidQueryHandle;
	UE::Editor::DataStorage::QueryHandle PackageReferenceAdded = UE::Editor::DataStorage::InvalidQueryHandle;

	// Query to fetch all rows with overlays
	UE::Editor::DataStorage::QueryHandle UpdateOverlays = UE::Editor::DataStorage::InvalidQueryHandle;

};
