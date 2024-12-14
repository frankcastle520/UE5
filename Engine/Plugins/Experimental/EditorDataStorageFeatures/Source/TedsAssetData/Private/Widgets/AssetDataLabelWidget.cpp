// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/AssetDataLabelWidget.h"

#include "AssetDefinition.h"
#include "AssetDefinitionRegistry.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "TedsAssetDataColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FAssetDataLabelWidgetConstructor"

void UAssetDataLabelWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
                                                              IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FAssetDataLabelWidgetConstructor>(
		TEXT("General.RowLabel"),
		TColumn<FNameColumn>() && (TColumn<FAssetTag>() || TColumn<FAssetPathColumn_Experimental>()));
}

FAssetDataLabelWidgetConstructor::FAssetDataLabelWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}

FAssetDataLabelWidgetConstructor::FAssetDataLabelWidgetConstructor(const UScriptStruct* TypeInfo)
	: FSimpleWidgetConstructor(TypeInfo)
{
}

TSharedPtr<SWidget> FAssetDataLabelWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow, UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	if (DataStorage->IsRowAvailable(TargetRow))
	{
		UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);

		bool bIsAsset = DataStorage->HasColumns<FAssetDataColumn_Experimental>(TargetRow);

		TAttribute<FSlateColor> ColorAndOpacityAttribute;

		// For assets, grab the color from the asset definition
		if(bIsAsset)
		{
			ColorAndOpacityAttribute = Binder.BindData(&FAssetDataColumn_Experimental::AssetData, [](const FAssetData& AssetData)
			{
				if(const UAssetDefinition* AssetDefinition = UAssetDefinitionRegistry::Get()->GetAssetDefinitionForAsset(AssetData))
				{
					return FSlateColor(AssetDefinition->GetAssetColor());
				}

				return FSlateColor::UseForeground();
			});
		}
		// For folders, use the color column directly
		else
		{
			ColorAndOpacityAttribute = Binder.BindData(&FSlateColorColumn::Color, FSlateColor::UseForeground());
		}

		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
				.AutoWidth()
			[
				SNew(SImage)
					.Image(bIsAsset ? FAppStyle::GetBrush(FName(TEXT("ContentBrowser.ColumnViewAssetIcon"))) : FAppStyle::GetBrush(FName(TEXT("ContentBrowser.ColumnViewFolderIcon"))))
					.ColorAndOpacity(ColorAndOpacityAttribute)
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
				.Text(Binder.BindText(&FNameColumn::Name))
				.ToolTipText(Binder.BindTextFormat(
								LOCTEXT("AssetLabelTooltip", 
									"{Name}\n\nVirtual path: {VirtualPath}\n  Asset path: {AssetPath}\n  Verse path: {VersePath}"))
								.Arg(TEXT("Name"), &FNameColumn::Name)
								.Arg(TEXT("VirtualPath"), &FVirtualPathColumn_Experimental::VirtualPath, LOCTEXT("PathNotSet", "<not set>"))
								.Arg(TEXT("AssetPath"), &FAssetPathColumn_Experimental::Path, LOCTEXT("PathNotSet", "<not set>"))
								.Arg(TEXT("VersePath"), &FVersePathColumn::VersePath, 
									[](const UE::Core::FVersePath& Path) 
									{
										return FText::FromStringView(Path.AsStringView());
									}, 
									LOCTEXT("PathNotSet", "<not set>")))
			];
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}
#undef LOCTEXT_NAMESPACE