// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Common/TypedElementCommonTypes.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Framework/TypedElementMetaData.h"
#include "Internationalization/Text.h"
#include "UObject/ObjectMacros.h"

#include "TransformHeadsUpWidget.generated.h"

/**
 * The heads up transform display provides at a glance view in a scene outliner row of abnormal transform characteristics, including:
 *		1. Non-uniform scale
 *		2. Negative scaling on X, Y, or Z axis
 *		3. Unnormalized rotation
 */
UCLASS()
class UTransformHeadsUpWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTransformHeadsUpWidgetFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;
	void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
};

USTRUCT()
struct FTransformHeadsUpWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	FTransformHeadsUpWidgetConstructor();
	~FTransformHeadsUpWidgetConstructor() override = default;

	TConstArrayView<const UScriptStruct*> GetAdditionalColumnsList() const override;

protected:
	TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	bool FinalizeWidget(
		IEditorDataStorageProvider* DataStorage,
		IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row,
		const TSharedPtr<SWidget>& Widget) override;
};

USTRUCT(meta = (DisplayName = "Heads up display for transforms widget"))
struct FTransformHeadsUpWidgetTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};
