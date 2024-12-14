// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Common/TypedElementQueryConditions.h"

#include "ExportedTextWidget.generated.h"

class IEditorDataStorageProvider;
class UScriptStruct;

UCLASS()
class TEDSUI_API UExportedTextWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	virtual ~UExportedTextWidgetFactory() override = default;

	virtual void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;

	TSet<TWeakObjectPtr<const UScriptStruct>> RegisteredTypes;
};

USTRUCT()
struct TEDSUI_API FExportedTextWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FExportedTextWidgetConstructor();
	virtual ~FExportedTextWidgetConstructor() override = default;

	virtual TConstArrayView<const UScriptStruct*> GetAdditionalColumnsList() const override;
	virtual const UE::Editor::DataStorage::Queries::FConditions* GetQueryConditions() const override;
	virtual FString CreateWidgetDisplayName(
		IEditorDataStorageProvider* DataStorage, UE::Editor::DataStorage::RowHandle Row) const override;

	virtual TSharedPtr<SWidget> ConstructFinalWidget(
		RowHandle Row, /** The row the widget will be stored in. */
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	
protected:
	virtual TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	virtual bool FinalizeWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row,
		const TSharedPtr<SWidget>& Widget) override;

protected:
	// The column this exported text widget is operating on
	UE::Editor::DataStorage::Queries::FConditions MatchedColumn;
};

USTRUCT(meta = (DisplayName = "Exported text widget"))
struct TEDSUI_API FExportedTextWidgetTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};