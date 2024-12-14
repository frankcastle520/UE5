// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTable/Widgets/OverrideWidgetConstructor.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "HierarchyTable/Columns/OverrideColumn.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "HierarchyTable.h"
#include "Columns/UIPropertiesColumns.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FTypedElementWidgetConstructor_Override"

FTypedElementWidgetConstructor_Override::FTypedElementWidgetConstructor_Override()
	: Super(StaticStruct())
{ 
}

TSharedPtr<SWidget> FTypedElementWidgetConstructor_Override::CreateWidget(
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center);
}

bool FTypedElementWidgetConstructor_Override::FinalizeWidget(
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	UE::Editor::DataStorage::RowHandle Row,
	const TSharedPtr<SWidget>& Widget)
{
	if (!Widget)
	{
		return true;
	}

	checkf(Widget->GetType() == SBox::StaticWidgetClass().GetWidgetType(),
		TEXT("Stored widget with FTypedElementFloatWidgetConstructor doesn't match type %s, but was a %s."),
		*(SBox::StaticWidgetClass().GetWidgetType().ToString()),
		*(Widget->GetTypeAsString()));

	TSharedPtr<SBox> WidgetInstance = StaticCastSharedPtr<SBox>(Widget);

	// Row is not actually the row we want, its contained inside of a row reference, I don't know why things are done this way.
	const UE::Editor::DataStorage::RowHandle TargetRow = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row)->Row;
	FTypedElementOverrideColumn* OverrideColumn = DataStorage->GetColumn<FTypedElementOverrideColumn>(TargetRow);

	if (OverrideColumn == nullptr)
	{
		check(false);
		WidgetInstance->SetContent(SNullWidget::NullWidget);

		return true;
	}

	UHierarchyTable* HierarchyTable = OverrideColumn->OwnerTable;
	const int32 EntryIndex = OverrideColumn->OwnerEntryIndex;

	const bool bHasParent = HierarchyTable->TableData[EntryIndex].HasParent();

	TSharedRef<SWidget> NewWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.IsEnabled(bHasParent)
			.OnClicked_Lambda([HierarchyTable, EntryIndex]()
				{
					const FScopedTransaction Transaction(LOCTEXT("ToggleOverride", "Toggle Override"));
					HierarchyTable->Modify();
					HierarchyTable->TableData[EntryIndex].ToggleOverridden();
					return FReply::Handled();
				})
			.ContentPadding(0.0f)
				[
					SNew(SImage)
					.Image_Lambda([HierarchyTable, EntryIndex]()
						{
							const FHierarchyTableEntryData& EntryData = HierarchyTable->TableData[EntryIndex];
							const bool bHasOverriddenChildren = EntryData.HasOverriddenChildren();

							if (EntryData.IsOverridden())
							{
								if (bHasOverriddenChildren)
								{
									return FAppStyle::GetBrush(TEXT("DetailsView.OverrideHereInside"));
								}
								else
								{
									return FAppStyle::GetBrush(TEXT("DetailsView.OverrideHere"));
								}
							}
							else
							{
								if (bHasOverriddenChildren)
								{
									return FAppStyle::GetBrush(TEXT("DetailsView.OverrideInside"));
								}
								else
								{
									return FAppStyle::GetBrush(TEXT("DetailsView.OverrideNone"));
								}
							}
						})
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
		];

	WidgetInstance->SetContent(NewWidget);

	return true;
}

//
// FTypedElementWidgetHeaderConstructor_Override
//

FTypedElementWidgetHeaderConstructor_Override::FTypedElementWidgetHeaderConstructor_Override()
	: Super(StaticStruct())
{
}

TSharedPtr<SWidget> FTypedElementWidgetHeaderConstructor_Override::CreateWidget(
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(SImage)
		.DesiredSizeOverride(FVector2D(16.f, 16.f))
		.ColorAndOpacity(FSlateColor::UseForeground())
		.Image(FAppStyle::GetBrush("DetailsView.OverrideHere"))
		.ToolTipText(FText(LOCTEXT("OverrideColumnHeader", "Overrides")));
}

bool FTypedElementWidgetHeaderConstructor_Override::FinalizeWidget(IEditorDataStorageProvider* DataStorage, 
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	DataStorage->AddColumn(Row, FUIHeaderPropertiesColumn
		{
			.ColumnSizeMode = EColumnSizeMode::Fixed,
			.Width = 24.0f
		});
	return true;
}

#undef LOCTEXT_NAMESPACE