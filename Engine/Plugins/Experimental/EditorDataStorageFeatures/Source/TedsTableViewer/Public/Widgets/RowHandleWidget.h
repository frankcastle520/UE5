// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Common/TypedElementQueryConditions.h"

#include "RowHandleWidget.generated.h"

class IEditorDataStorageProvider;
class UScriptStruct;

UCLASS()
class TEDSTABLEVIEWER_API URowHandleWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	virtual ~URowHandleWidgetFactory() override = default;

	virtual void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;

	void RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const override;
};

// A custom widget to display the row handle of a row as text
USTRUCT()
struct TEDSTABLEVIEWER_API FRowHandleWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FRowHandleWidgetConstructor();
	virtual ~FRowHandleWidgetConstructor() override = default;

protected:
	virtual TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	virtual bool FinalizeWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row,
		const TSharedPtr<SWidget>& Widget) override;
};
