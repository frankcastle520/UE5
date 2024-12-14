// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "HierarchyTableWidgetConstructor.generated.h"

class UHierarchyTable;
struct FHierarchyTableEntryData;

USTRUCT()
struct HIERARCHYTABLEEDITOR_API FHierarchyTableWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	explicit FHierarchyTableWidgetConstructor() : Super(StaticStruct()) {}
	explicit FHierarchyTableWidgetConstructor(const UScriptStruct* InTypeInfo);

	virtual ~FHierarchyTableWidgetConstructor() override = default;

	virtual TSharedRef<SWidget> CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex);

protected:
	// Begin FHierarchyTableWidgetConstructor
	TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments);

	bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
	// End FHierarchyTableWidgetConstructor
};