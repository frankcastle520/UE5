// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/TypeInfoWidget.h"

#include "SceneOutlinerHelpers.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiTextCapability.h"
#include "Styling/SlateIconFinder.h"
#include "TedsTableViewerUtils.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"

void UTypeInfoWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FTypeInfoWidgetConstructor>(FName(TEXT("General.Cell")),
		TColumn<FTypedElementClassTypeInfoColumn>());

}

FTypeInfoWidgetConstructor::FTypeInfoWidgetConstructor()
	: Super(FTypeInfoWidgetConstructor::StaticStruct())
	, bUseIcon(false)
{
}

FTypeInfoWidgetConstructor::FTypeInfoWidgetConstructor(const UScriptStruct* InTypeInfo)
	: Super(InTypeInfo)
	, bUseIcon(false)
{
	
}

TSharedPtr<SWidget> FTypeInfoWidgetConstructor::CreateWidget(
	const UE::Editor::DataStorage::FMetaDataView& Arguments	)
{
	bUseIcon = false;
	
	// Check if the caller provided metadata to use an icon widget
	UE::Editor::DataStorage::FMetaDataEntryView MetaDataEntryView = Arguments.FindGeneric("TypeInfoWidget_bUseIcon");
	if(MetaDataEntryView.IsSet())
	{
		check(MetaDataEntryView.IsType<bool>());

		bUseIcon = *MetaDataEntryView.TryGetExact<bool>();
	}

	if(bUseIcon)
	{
		return SNew(SImage)
					.DesiredSizeOverride(FVector2D(16.f, 16.f))
					.ColorAndOpacity(FSlateColor::UseForeground());
	}
	else
	{
		return SNew(SHorizontalBox);
	}
}

bool FTypeInfoWidgetConstructor::FinalizeWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	checkf(Widget, TEXT("Referenced widget is not valid. A constructed widget may not have been cleaned up. This can "
		"also happen if this processor is running in the same phase as the processors responsible for cleaning up old "
		"references."));

	UE::Editor::DataStorage::RowHandle TargetRow = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row)->Row;

	if (const FTypedElementClassTypeInfoColumn* TypeInfoColumn = DataStorage->GetColumn<FTypedElementClassTypeInfoColumn>(TargetRow))
	{
		if(bUseIcon)
		{
			checkf(Widget->GetType() == SImage::StaticWidgetClass().GetWidgetType(),
				TEXT("Stored widget with FTypedElementLabelWidgetConstructor doesn't match type %s, but was a %s."),
				*(SImage::StaticWidgetClass().GetWidgetType().ToString()),
				*(Widget->GetTypeAsString()));

			SImage* WidgetInstance = static_cast<SImage*>(Widget.Get());
			
			WidgetInstance->SetImage(UE::Editor::DataStorage::TableViewerUtils::GetIconForRow(DataStorage, Row));
		}
		else
		{
			checkf(Widget->GetType() == SHorizontalBox::StaticWidgetClass().GetWidgetType(),
				TEXT("Stored widget with FTypedElementLabelWidgetConstructor doesn't match type %s, but was a %s."),
				*(SHorizontalBox::StaticWidgetClass().GetWidgetType().ToString()),
				*(Widget->GetTypeAsString()));
				
			SHorizontalBox* WidgetInstance = static_cast<SHorizontalBox*>(Widget.Get());

			TSharedPtr<SWidget> ActualWidget;

			// Check if we have a hyperlink for this object
			if (const FTypedElementUObjectColumn* ObjectColumn = DataStorage->GetColumn<FTypedElementUObjectColumn>(TargetRow))
			{
				ActualWidget = SceneOutliner::FSceneOutlinerHelpers::GetClassHyperlink(ObjectColumn->Object.Get());
			}

			// If not, we simply show a text block with the type
			if(!ActualWidget)
			{
				TSharedPtr<STextBlock> TextBlock = SNew(STextBlock)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Text(FText::FromString(TypeInfoColumn->TypeInfo.Get()->GetName()));

				TextBlock->AddMetadata(MakeShared<TTypedElementUiTextCapability<STextBlock>>(*TextBlock));
				ActualWidget = TextBlock;
			}

			WidgetInstance->AddSlot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8, 0, 0, 0)
						[
							ActualWidget.ToSharedRef()
						];
		}
	}

	return true;
}
