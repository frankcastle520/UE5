// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableLabelWidget.h"

#include "HierarchyTable/Columns/OverrideColumn.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "HierarchyTable.h"

#define LOCTEXT_NAMESPACE "HierarchyTableLabelWidget"

void UHierarchyTableLabelWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FHierarchyTableLabelWidgetConstructor>(
		TEXT("General.RowLabel"),
		TColumn<FTypedElementLabelColumn>() && TColumn<FTypedElementOverrideColumn>());
}

FHierarchyTableLabelWidgetConstructor::FHierarchyTableLabelWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}

FHierarchyTableLabelWidgetConstructor::FHierarchyTableLabelWidgetConstructor(const UScriptStruct* TypeInfo)
	: FSimpleWidgetConstructor(TypeInfo)
{
}

TSharedPtr<SWidget> FHierarchyTableLabelWidgetConstructor::CreateWidget(
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	RowHandle TargetRow,
	RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	using namespace UE::Editor::DataStorage;
	if(DataStorage->IsRowAvailable(TargetRow))
	{
		FAttributeBinder Binder(TargetRow, DataStorage);

		FTypedElementOverrideColumn* OverrideColumn = DataStorage->GetColumn<FTypedElementOverrideColumn>(TargetRow);
		const FHierarchyTableEntryData& EntryData = OverrideColumn->OwnerTable->TableData[OverrideColumn->OwnerEntryIndex];

		const FSlateBrush* LabelIcon = nullptr;
		FSlateColor LabelIconColor = FSlateColor::UseForeground();

		switch (EntryData.EntryType)
		{
			case EHierarchyTableEntryType::Bone:
				LabelIcon = FAppStyle::GetBrush("SkeletonTree.Bone");
				break;
			case EHierarchyTableEntryType::Curve:
				LabelIcon = FAppStyle::GetBrush("AnimGraph.Attribute.Curves.Icon");
				LabelIconColor = FAppStyle::GetSlateColor("AnimGraph.Attribute.Curves.Color");
				break;
			case EHierarchyTableEntryType::Attribute:
				LabelIcon = FAppStyle::GetBrush("AnimGraph.Attribute.Attributes.Icon");
				LabelIconColor = FAppStyle::GetSlateColor("AnimGraph.Attribute.Attributes.Color");
				break;
		}

		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
				.AutoWidth()
			[
				SNew(SImage)
					.Image(LabelIcon)
					.ColorAndOpacity(LabelIconColor)
			]
			+SHorizontalBox::Slot()
				.AutoWidth()
			[
				SNew(SSpacer)
					.Size(FVector2D(5.0f, 0.0f))
			]
			+SHorizontalBox::Slot()
				.FillWidth(1.0f)
			[
				SNew(STextBlock)
					.Text(Binder.BindText(&FTypedElementLabelColumn::Label))
					.ToolTipText(Binder.BindText(&FTypedElementLabelColumn::Label))
			];
	}
	else
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("MissingRowReferenceColumn", "Unable to retrieve row reference."));
	}
	
	
}

#undef LOCTEXT_NAMESPACE
