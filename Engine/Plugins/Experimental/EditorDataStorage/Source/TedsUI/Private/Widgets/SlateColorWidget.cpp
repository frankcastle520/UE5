// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SlateColorWidget.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Internationalization/Text.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorPicker.h"

namespace UE::SlateColorWidget::Private
{
	FReply SummonColorPicker(const FGeometry& Geometry, const FPointerEvent& PointerEvent,
		IEditorDataStorageProvider* DataStorage, UE::Editor::DataStorage::RowHandle TargetRow)
	{
		if (PointerEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
	
		FColorPickerArgs PickerArgs;
		PickerArgs.bUseAlpha = true;
		PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([TargetRow, DataStorage](FLinearColor Color)
		{
			DataStorage->GetColumn<FSlateColorColumn>(TargetRow)->Color = Color;
			DataStorage->AddColumn(TargetRow, FTypedElementSyncBackToWorldTag::StaticStruct());
		});

		FSlateColor ColumnColor = DataStorage->GetColumn<FSlateColorColumn>(TargetRow)->Color;
		// If the color isn't set to a specified value, we just show it as white by default in the picker
		FLinearColor InitColor = ColumnColor.IsColorSpecified() ? ColumnColor.GetSpecifiedColor() : FLinearColor::White;
	
		PickerArgs.InitialColor = InitColor;
		PickerArgs.bOnlyRefreshOnOk = true;
		OpenColorPicker(PickerArgs);
		return FReply::Handled();
	}
}

void USlateColorWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
														IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FSlateColorWidgetConstructor>(TEXT("General.Cell"), TColumn<FSlateColorColumn>());
}

FSlateColorWidgetConstructor::FSlateColorWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}



TSharedPtr<SWidget> FSlateColorWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
                                                               IEditorDataStorageUiProvider* DataStorageUi, RowHandle TargetRow, RowHandle WidgetRow,
                                                               const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);

	FPointerEventHandler OnMouseButtonDown;
	const bool* IsEditable = Arguments.FindForColumn<FSlateColorColumn>(UE::Editor::DataStorage::IsEditableName).TryGetExact<bool>();

	// If the column is editable, summon a color picker on click
	if(IsEditable && *IsEditable)
	{
		OnMouseButtonDown = FPointerEventHandler::CreateStatic(&UE::SlateColorWidget::Private::SummonColorPicker, DataStorage, TargetRow);
	}
	
	return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SColorBlock)
					.Color(Binder.BindData(&FSlateColorColumn::Color, [](const FSlateColor& Color)
					{
						// If the color isn't set to a specified value, we just show it as white by default in the picker
						return Color.IsColorSpecified() ? Color.GetSpecifiedColor() : FLinearColor::White;
					}))
					.Size(FVector2D(64, 16))
					.OnMouseButtonDown(OnMouseButtonDown)
			];
}
