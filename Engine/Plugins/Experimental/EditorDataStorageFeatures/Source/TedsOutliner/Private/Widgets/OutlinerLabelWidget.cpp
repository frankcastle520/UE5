// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/OutlinerLabelWidget.h"

#include "ActorEditorUtils.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "TedsTableViewerUtils.h"
#include "Columns/SlateDelegateColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementOverrideColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiEditableCapability.h"
#include "Elements/Interfaces/Capabilities/TypedElementUiStyleOverrideCapability.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "FOutlinerLabelWidgetConstructor"

namespace UE::OutlinerLabelWidget::Local
{
	const FSlateBrush* GetOverrideBadgeFirstLayer(const EOverriddenState& OverriddenState)
	{
		switch(OverriddenState)
		{
		case EOverriddenState::Added:
			return FAppStyle::GetBrush("SceneOutliner.OverrideAddedBase");
			
		case EOverriddenState::AllOverridden:
			// Not implemented yet
			break;
			
		case EOverriddenState::HasOverrides:
			return FAppStyle::GetBrush("SceneOutliner.OverrideInsideBase");
			
		case EOverriddenState::NoOverrides:
			// No icon for no overrides
			break;
			
		case EOverriddenState::SubObjectsHasOverrides:
			// Not implemented yet
			break;
		}
		
		return FAppStyle::GetBrush("NoBrush");
	}
	
	const FSlateBrush* GetOverrideBadgeSecondLayer(const EOverriddenState& OverriddenState)
	{
		switch(OverriddenState)
		{
		case EOverriddenState::Added:
			return FAppStyle::GetBrush("SceneOutliner.OverrideAdded");
			
		case EOverriddenState::AllOverridden:
			// Not implemented yet
				break;
			
		case EOverriddenState::HasOverrides:
			return FAppStyle::GetBrush("SceneOutliner.OverrideInside");
			
		case EOverriddenState::NoOverrides:
			// No icon for no overrides
				break;
			
		case EOverriddenState::SubObjectsHasOverrides:
			// Not implemented yet
				break;
		}

		return FAppStyle::GetBrush("NoBrush");
	}

	FText GetOverrideTooltip(const EOverriddenState& OverriddenState)
	{
		switch(OverriddenState)
		{
		case EOverriddenState::Added:
			return LOCTEXT("OverrideAddedTooltip", "This entity has been added.");
			
		case EOverriddenState::AllOverridden:
			// Not implemented yet
				break;
			
		case EOverriddenState::HasOverrides:
			return LOCTEXT("OverrideInsideTooltip", "At least one property or child has an override.");
			
		case EOverriddenState::NoOverrides:
			// No icon for no overrides
				break;
			
		case EOverriddenState::SubObjectsHasOverrides:
			// Not implemented yet
				break;
		}

		return FText::GetEmpty();
	}
}

void UOutlinerLabelWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FOutlinerLabelWidgetConstructor>(
		TEXT("SceneOutliner.RowLabel"),
		TColumn<FTypedElementLabelColumn>() && TColumn<FTypedElementClassTypeInfoColumn>());
}

FOutlinerLabelWidgetConstructor::FOutlinerLabelWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}

TSharedPtr<SWidget> FOutlinerLabelWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, RowHandle TargetRow, RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	using namespace UE::Editor::DataStorage;
	if(DataStorage->IsRowAvailable(TargetRow))
	{
		FAttributeBinder Binder(TargetRow, DataStorage);

		TSharedRef<SLayeredImage> LayeredImageWidget = SNew(SLayeredImage)
					.Image(TableViewerUtils::GetIconForRow(DataStorage, TargetRow))
					.ToolTipText(Binder.BindData(&FObjectOverrideColumn::OverriddenState, [](const EOverriddenState& OverriddenState)
					{
						return UE::OutlinerLabelWidget::Local::GetOverrideTooltip(OverriddenState);
					}))
					.ColorAndOpacity(FSlateColor::UseForeground());

		LayeredImageWidget->AddLayer(Binder.BindData(&FObjectOverrideColumn::OverriddenState, [](const EOverriddenState& OverriddenState)
		{
			return UE::OutlinerLabelWidget::Local::GetOverrideBadgeFirstLayer(OverriddenState);
		}));
		
		LayeredImageWidget->AddLayer(Binder.BindData(&FObjectOverrideColumn::OverriddenState, [](const EOverriddenState& OverriddenState)
		{
			return UE::OutlinerLabelWidget::Local::GetOverrideBadgeSecondLayer(OverriddenState);
		}));

		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
				.AutoWidth()
			[
				LayeredImageWidget
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
				CreateLabel(DataStorage, DataStorageUi, TargetRow, WidgetRow, Arguments)
			];
	}
	else
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("MissingRowReferenceColumn", "Unable to retrieve row reference."));
	}
	
	
}

TSharedRef<SWidget> FOutlinerLabelWidgetConstructor::CreateLabel(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, RowHandle TargetRow, RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	using namespace UE::Editor::DataStorage;
	TSharedRef<SWidget> Result = SNullWidget::NullWidget;
	
	const bool* IsEditable = Arguments.FindForColumn<FTypedElementLabelColumn>(IsEditableName).TryGetExact<bool>();

	if (IsEditable && *IsEditable)
	{
		FAttributeBinder TargetRowBinder(TargetRow, DataStorage);
		FAttributeBinder WidgetRowBinder(WidgetRow, DataStorage);
			
		TSharedPtr<SInlineEditableTextBlock> TextBlock = SNew(SInlineEditableTextBlock)
			.OnTextCommitted_Lambda(
				[DataStorage, TargetRow](const FText& NewText, ETextCommit::Type CommitInfo)
				{
					// This callback happens on the game thread so it's safe to directly call into the data storage.
					FString NewLabelText = NewText.ToString();
					if (FTypedElementLabelHashColumn* LabelHashColumn = DataStorage->GetColumn<FTypedElementLabelHashColumn>(TargetRow))
					{
						LabelHashColumn->LabelHash = CityHash64(reinterpret_cast<const char*>(*NewLabelText), NewLabelText.Len() * sizeof(**NewLabelText));
					}
					if (FTypedElementLabelColumn* LabelColumn = DataStorage->GetColumn<FTypedElementLabelColumn>(TargetRow))
					{
						LabelColumn->Label = MoveTemp(NewLabelText);
					}
					DataStorage->AddColumn<FTypedElementSyncBackToWorldTag>(TargetRow);
				})
			.OnVerifyTextChanged_Lambda([](const FText& Label, FText& ErrorMessage)
				{
					// Note: The use of actor specific functionality should be minimized, but this function acts generic enough that the 
					// use of actor is just in names.
					return FActorEditorUtils::ValidateActorName(Label, ErrorMessage);
				})
			.Text(TargetRowBinder.BindText(&FTypedElementLabelColumn::Label))
			.ToolTipText(TargetRowBinder.BindText(&FTypedElementLabelColumn::Label))
			.IsSelected(WidgetRowBinder.BindEvent(&FExternalWidgetSelectionColumn::IsSelected));

		TextBlock->AddMetadata(MakeShared<TTypedElementUiEditableCapability<SInlineEditableTextBlock>>(*TextBlock));
		TextBlock->AddMetadata(MakeShared<TTypedElementUiStyleOverrideCapability<SInlineEditableTextBlock>>(*TextBlock));
		Result = TextBlock.ToSharedRef();
	}
	else
	{
		FAttributeBinder TargetRowBinder(TargetRow, DataStorage);
		
		TSharedPtr<STextBlock> TextBlock = SNew(STextBlock)
			.IsEnabled(false)
			.Text(TargetRowBinder.BindText(&FTypedElementLabelColumn::Label))
			.ToolTipText(TargetRowBinder.BindText(&FTypedElementLabelColumn::Label));
		
		Result = TextBlock.ToSharedRef();
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE //"FOutlinerLabelWidgetConstructor"
