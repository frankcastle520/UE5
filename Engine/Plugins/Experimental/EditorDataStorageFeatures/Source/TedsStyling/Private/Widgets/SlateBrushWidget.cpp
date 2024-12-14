// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SlateBrushWidget.h"

#include "Columns/TedsStylingColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void USlateStylePreviewWidget::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FSlateStylePreviewWidgetConstructor>(TEXT("General.RowLabel"), TColumn<FNameColumn>() && TColumn<FSlateStyleTag>());
	DataStorageUi.RegisterWidgetFactory<FSlateStylePreviewWidgetConstructor>(TEXT("General.Cell"), TColumn<FNameColumn>() && TColumn<FSlateStyleTag>());
}

FSlateStylePreviewWidgetConstructor::FSlateStylePreviewWidgetConstructor()
	: Super(StaticStruct())
{
}

TSharedPtr<SWidget> FSlateStylePreviewWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow, UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);
	
	return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBox)
				.MaxDesiredHeight(16.0f)
				.MaxDesiredWidth(16.0f)
				[
					// Currently we cannot differentiate between image brushes and other brushes in TEDS so we only show image brushes as an SImage
					// and don't support showing a preview of the rest
					SNew(SImage)
					.Image_Lambda([DataStorage, TargetRow]()
					{
						const FNameColumn* StyleNameColumn = DataStorage->GetColumn<FNameColumn>(TargetRow);
						const FSlateStyleSetColumn* StyleSetColumn = DataStorage->GetColumn<FSlateStyleSetColumn>(TargetRow);

						if (StyleNameColumn && StyleSetColumn && DataStorage->HasColumns<FSlateBrushTag>(TargetRow))
						{
							if(const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(StyleSetColumn->StyleSetName))
							{
								if(const FSlateBrush* Brush = Style->GetBrush(StyleNameColumn->Name))
								{
									return Brush;
								}
							}
						}
						return FAppStyle::GetBrush("NoBrush");
					})
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Binder.BindText(&FNameColumn::Name))
			];
	
}
