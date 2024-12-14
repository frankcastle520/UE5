// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "SlateColorWidget.generated.h"

class IEditorDataStorageProvider;
class IEditorDataStorageUiProvider;
class UScriptStruct;

UCLASS()
class USlateColorWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~USlateColorWidgetFactory() override = default;

	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
};

// Widget to show and edit the color column in TEDS
USTRUCT()
struct FSlateColorWidgetConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

public:
	FSlateColorWidgetConstructor();
	~FSlateColorWidgetConstructor() override = default;

	virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		RowHandle TargetRow,
		RowHandle WidgetRow, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
};