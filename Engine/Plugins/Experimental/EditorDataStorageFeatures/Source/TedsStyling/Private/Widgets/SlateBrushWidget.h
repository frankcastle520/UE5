// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "SlateBrushWidget.generated.h"

class IEditorDataStorageProvider;
class IEditorDataStorageUiProvider;
class UScriptStruct;

UCLASS()
class USlateStylePreviewWidget : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~USlateStylePreviewWidget() override = default;

	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
};

// Widget to show a slate brush drawn as an SImage
USTRUCT()
struct FSlateStylePreviewWidgetConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

public:
	FSlateStylePreviewWidgetConstructor();
	~FSlateStylePreviewWidgetConstructor() override = default;

	virtual TSharedPtr<SWidget> CreateWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow,
		const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
};