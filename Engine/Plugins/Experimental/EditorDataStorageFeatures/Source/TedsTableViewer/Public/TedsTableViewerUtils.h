// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/Class.h"

#include "TedsTableViewerUtils.generated.h"

namespace UE::Editor::DataStorage
{
	class FMetaDataView;
}

class IEditorDataStorageProvider;
class IEditorDataStorageUiProvider;
struct FTypedElementWidgetConstructor;
struct FSlateBrush;

// Util library for functions shared by the Teds Table Viewer and the Teds Outliner
namespace UE::Editor::DataStorage::TableViewerUtils
{
	TEDSTABLEVIEWER_API FName GetWidgetTableName();
	
	// Find the longest matching common column name given a list of columns
	TEDSTABLEVIEWER_API FName FindLongestMatchingName(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes, int32 DefaultNameIndex);

	// Create a header widget constructor for the given columns
	TEDSTABLEVIEWER_API TSharedPtr<FTypedElementWidgetConstructor> CreateHeaderWidgetConstructor(IEditorDataStorageUiProvider& StorageUi, 
	const UE::Editor::DataStorage::FMetaDataView& InMetaData, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes,
	const TConstArrayView<FName> CellWidgetPurposes);

	// Create a copy of the provided column types array after discarding invalid entries
	TEDSTABLEVIEWER_API TArray<TWeakObjectPtr<const UScriptStruct>> CreateVerifiedColumnTypeArray(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes);
	
	// Scans the row for columns and tries to find the best matching icon.
	TEDSTABLEVIEWER_API const FSlateBrush* GetIconForRow(IEditorDataStorageProvider* DataStorage, RowHandle Row);
} // namespace UE::Editor::DataStorage::TableViewerUtils

UCLASS()
class UTypedElementTableViewerFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypedElementTableViewerFactory() override = default;

	void RegisterTables(IEditorDataStorageProvider& DataStorage) override;
};

