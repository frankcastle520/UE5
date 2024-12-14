// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "OverrideWidgetConstructor.generated.h"

// TODO: Can be converted into a child of FHierarchyTableWidgetConstructor
USTRUCT()
struct FTypedElementWidgetConstructor_Override : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FTypedElementWidgetConstructor_Override();
	~FTypedElementWidgetConstructor_Override() override = default;

protected:
	TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments);
	bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
};

USTRUCT()
struct FTypedElementWidgetHeaderConstructor_Override : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FTypedElementWidgetHeaderConstructor_Override();
	~FTypedElementWidgetHeaderConstructor_Override() override = default;

protected:
	TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
};