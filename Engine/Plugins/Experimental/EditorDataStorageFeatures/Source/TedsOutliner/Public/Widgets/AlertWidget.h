// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Common/TypedElementCommonTypes.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#include "AlertWidget.generated.h"

UCLASS()
class UAlertWidgetFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	TEDSOUTLINER_API ~UAlertWidgetFactory() override = default;

	TEDSOUTLINER_API void RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
		IEditorDataStorageUiProvider& DataStorageUi) const override;
	TEDSOUTLINER_API void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	void RegisterAlertQueries(IEditorDataStorageProvider& DataStorage);
	void RegisterAlertHeaderQueries(IEditorDataStorageProvider& DataStorage);
};

USTRUCT()
struct FAlertWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	static constexpr int32 IconBackgroundSlot = 1;
	static constexpr int32 IconBadgeSlot = 2;
	static constexpr int32 CounterTextSlot = 3;
	static constexpr int32 ActionButtonSlot = 0;

	static constexpr float BadgeFontSize = 7.0f;
	static constexpr float BadgeHorizontalOffset = 13.0f;
	static constexpr float BadgeVerticalOffset = 1.0f;

	TEDSOUTLINER_API FAlertWidgetConstructor();
	TEDSOUTLINER_API ~FAlertWidgetConstructor() override = default;

protected:
	TEDSOUTLINER_API TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	TEDSOUTLINER_API TConstArrayView<const UScriptStruct*> GetAdditionalColumnsList() const override;
	TEDSOUTLINER_API bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
};

USTRUCT(meta = (DisplayName = "General purpose alert"))
struct FAlertWidgetTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FAlertHeaderWidgetConstructor : public FTypedElementWidgetConstructor
{
	GENERATED_BODY()

public:
	TEDSOUTLINER_API FAlertHeaderWidgetConstructor();
	TEDSOUTLINER_API ~FAlertHeaderWidgetConstructor() override = default;

protected:
	TEDSOUTLINER_API TSharedPtr<SWidget> CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
	TEDSOUTLINER_API TConstArrayView<const UScriptStruct*> GetAdditionalColumnsList() const override;
	TEDSOUTLINER_API bool FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
		UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget) override;
};

USTRUCT(meta = (DisplayName = "General purpose alert header"))
struct FAlertHeaderWidgetTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Alert header active"))
struct FAlertHeaderActiveWidgetTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};
