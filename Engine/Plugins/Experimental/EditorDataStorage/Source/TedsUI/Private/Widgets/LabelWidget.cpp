// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/LabelWidget.h"

#include "ActorEditorUtils.h"
#include "Columns/SlateDelegateColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementValueCacheColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiEditableCapability.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiTextCapability.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiTooltipCapability.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiStyleOverrideCapability.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "TedsUI_LabelWidget"

//
// ULabelWidgetFactory
//

void ULabelWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FLabelWidgetConstructor>(FName(TEXT("General.Cell")), 
		TColumn<FTypedElementLabelColumn>() || (TColumn<FTypedElementLabelColumn>() && TColumn<FTypedElementLabelHashColumn>()));

	DataStorageUi.RegisterWidgetFactory<FLabelWidgetConstructor>(FName(TEXT("General.RowLabel")), 
		TColumn<FTypedElementLabelColumn>() || (TColumn<FTypedElementLabelColumn>() && TColumn<FTypedElementLabelHashColumn>()));
}

void ULabelWidgetFactory::RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetPurpose(FName(TEXT("General.RowLabel")), IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn,
	LOCTEXT("GeneralRowLabelPurpose", "Specific purpose to request a widget to display a user facing display name for a row."));
	
	DataStorageUi.RegisterWidgetPurpose(FName(TEXT("General.RowLabel.Default")), IEditorDataStorageUiProvider::EPurposeType::UniqueByName,
	LOCTEXT("GeneralRowLabelDefaultPurpose", "Default purpose to request a widget to display a user facing display name for a row."));
}


//
// FLabelWidgetConstructor
//

FLabelWidgetConstructor::FLabelWidgetConstructor()
	: Super(StaticStruct())
{
}

TConstArrayView<const UScriptStruct*> FLabelWidgetConstructor::GetAdditionalColumnsList() const
{
	static const TTypedElementColumnTypeList<
		FTypedElementRowReferenceColumn,
		FTypedElementU64IntValueCacheColumn,
		FExternalWidgetSelectionColumn> Columns;
	return Columns;
}

TSharedPtr<SWidget> FLabelWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, RowHandle TargetRow, RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);

	return SNew(STextBlock)
		.Text(Binder.BindText(&FTypedElementLabelColumn::Label))
		.ToolTipText(Binder.BindText(&FTypedElementLabelColumn::Label));
}

#undef LOCTEXT_NAMESPACE
