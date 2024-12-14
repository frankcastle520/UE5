// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableWidgetConstructor.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "HierarchyTable/Columns/OverrideColumn.h"

FHierarchyTableWidgetConstructor::FHierarchyTableWidgetConstructor(const UScriptStruct* InTypeInfo)
	: Super(InTypeInfo)
{
}

TSharedRef<SWidget> FHierarchyTableWidgetConstructor::CreateInternalWidget(UHierarchyTable* HierarchyTable, int32 EntryIndex)
{
	return SNullWidget::NullWidget;
}

TSharedPtr<SWidget> FHierarchyTableWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center);
}

bool FHierarchyTableWidgetConstructor::FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi,
	UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	TSharedPtr<SBox> WidgetInstance = StaticCastSharedPtr<SBox>(Widget);

	// Row is not actually the row we want, its contained inside of a row reference, I don't know why things are done this way.
	UE::Editor::DataStorage::RowHandle TargetRow = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row)->Row;
	FTypedElementOverrideColumn* OverrideColumn = DataStorage->GetColumn<FTypedElementOverrideColumn>(TargetRow);

	TSharedRef<SWidget> ActualWidget = CreateInternalWidget(OverrideColumn->OwnerTable, OverrideColumn->OwnerEntryIndex);
	WidgetInstance->SetContent(ActualWidget);

	return true;
}