// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "UObject/ObjectMacros.h"

#include "HierarchyTableLabelWidget.generated.h"

class IEditorDataStorageProvider;
class UScriptStruct;

UCLASS()
class UHierarchyTableLabelWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UHierarchyTableLabelWidgetFactory() override = default;

	void RegisterWidgetConstructors(
		IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
};

USTRUCT()
struct FHierarchyTableLabelWidgetConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

public:
	FHierarchyTableLabelWidgetConstructor();
	explicit FHierarchyTableLabelWidgetConstructor(const UScriptStruct* TypeInfo);
	~FHierarchyTableLabelWidgetConstructor() override = default;

	virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
};