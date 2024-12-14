// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "SlateVisualizationWidget.generated.h"

class IEditorDataStorageProvider;
class SWidget;

/*
 * Widget for the TEDS Debugger that shows a slate widget reference
 */
UCLASS()
class USlateVisualizationWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~USlateVisualizationWidgetFactory() override = default;

	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
};

USTRUCT()
struct FSlateVisualizationWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FSlateVisualizationWidgetConstructor();
	~FSlateVisualizationWidgetConstructor() override = default;

protected:
	TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
};