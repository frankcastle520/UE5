// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Internationalization/Text.h"
#include "UObject/ObjectMacros.h"

#include "LabelWidget.generated.h"

class IEditorDataStorageProvider;
class UScriptStruct;

UCLASS()
class ULabelWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~ULabelWidgetFactory() override = default;

	TEDSUI_API void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
	TEDSUI_API virtual void RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const override;
};

USTRUCT()
struct FLabelWidgetConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

public:
	TEDSUI_API FLabelWidgetConstructor();
	~FLabelWidgetConstructor() override = default;

	TEDSUI_API virtual TConstArrayView<const UScriptStruct*> GetAdditionalColumnsList() const override;

	TEDSUI_API virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow, 
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;

protected:
};