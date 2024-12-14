// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "RowReferenceWidget.generated.h"

class IEditorDataStorageProvider;
class SWidget;

/*
 * Widget for the TEDS Debugger that visualizes a reference to another row
 */
UCLASS()
class URowReferenceWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~URowReferenceWidgetFactory() override;

	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
	
	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
};

USTRUCT()
struct FRowReferenceWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FRowReferenceWidgetConstructor();
	~FRowReferenceWidgetConstructor() override = default;

protected:
	TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
};