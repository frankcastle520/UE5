// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/RowHandleWidget.h"

#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RowHandleWidget"


void URowHandleWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetFactory(FName(TEXT("General.Cell.RowHandle")), FRowHandleWidgetConstructor::StaticStruct());
}

void URowHandleWidgetFactory::RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetPurpose(FName(TEXT("General.Cell.RowHandle")), IEditorDataStorageUiProvider::EPurposeType::UniqueByName,
	LOCTEXT("GeneralRowHandlePurpose", "Specific purpose to request a widget to display row handles."));

	DataStorageUi.RegisterWidgetPurpose(FName(TEXT("RowDetails.Cell")), IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn,
	LOCTEXT("DetailsRowHandlePurpose", "Specific purpose to request a widget to display the details on a row (e.g SRowDetails)."));

	DataStorageUi.RegisterWidgetPurpose(FName(TEXT("RowDetails.Cell.Large")), IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn,
	LOCTEXT("LargeDetailsRowHandlePurpose", "Specific purpose to request a widget that is larger than a single cell to display the details on a row (e.g SRowDetails)"));


}

FRowHandleWidgetConstructor::FRowHandleWidgetConstructor()
	: Super(FRowHandleWidgetConstructor::StaticStruct())
{
	
}

TSharedPtr<SWidget> FRowHandleWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(8, 0, 0, 0);}

bool FRowHandleWidgetConstructor::FinalizeWidget(IEditorDataStorageProvider* DataStorage, IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle Row, const TSharedPtr<SWidget>& Widget)
{

	checkf(Widget->GetType() == SBox::StaticWidgetClass().GetWidgetType(),
		TEXT("Stored widget with FRowHandleWidgetConstructor doesn't match type %s, but was a %s."),
		*(SBox::StaticWidgetClass().GetWidgetType().ToString()),
		*(Widget->GetTypeAsString()));
	
	SBox* BoxWidget = static_cast<SBox*>(Widget.Get());

	UE::Editor::DataStorage::RowHandle TargetRowHandle = UE::Editor::DataStorage::InvalidRowHandle;

	if(const FTypedElementRowReferenceColumn* RowReferenceColumn = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row))
	{
		TargetRowHandle = RowReferenceColumn->Row;
	}
	
	FNumberFormattingOptions NumberFormattingOptions;
	NumberFormattingOptions.SetUseGrouping(false);
	const FText Text = FText::AsNumber(TargetRowHandle, &NumberFormattingOptions);

	BoxWidget->SetContent(
			SNew(STextBlock)
				.Text(Text)
				.ColorAndOpacity(FSlateColor::UseForeground())
		);
	return true;
}

#undef LOCTEXT_NAMESPACE //"RowHandleWidget"
