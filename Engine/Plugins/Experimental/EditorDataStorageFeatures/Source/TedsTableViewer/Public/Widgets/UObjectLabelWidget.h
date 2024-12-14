// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Internationalization/Text.h"
#include "UObject/ObjectMacros.h"

#include "UObjectLabelWidget.generated.h"

class IEditorDataStorageProvider;
class UScriptStruct;

UCLASS()
class UUObjectLabelWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UUObjectLabelWidgetFactory() override = default;

	TEDSTABLEVIEWER_API void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
};

// Widget to show labels for UObjects in TEDS
USTRUCT()
struct FUObjectLabelWidgetConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

public:
	TEDSTABLEVIEWER_API FUObjectLabelWidgetConstructor();
	TEDSTABLEVIEWER_API explicit FUObjectLabelWidgetConstructor(const UScriptStruct* TypeInfo);
	~FUObjectLabelWidgetConstructor() override = default;

	TEDSTABLEVIEWER_API virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;

protected:
};