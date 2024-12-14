// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorCueListRow.h"

#include "DMXControlConsoleEditorData.h"
#include "DMXEditorStyle.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Models/DMXControlConsoleCueStackModel.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorCueListRow"

namespace UE::DMX::Private
{ 
	void SDMXControlConsoleEditorCueListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TSharedRef<FDMXControlConsoleEditorCueListItem>& InItem, TSharedPtr<FDMXControlConsoleCueStackModel> InCueStackModel)
	{
		if (!InCueStackModel.IsValid())
		{
			return;
		}

		WeakCueStackModel = InCueStackModel;
		Item = InItem;

		OnEditCueItemColorDelegate = InArgs._OnEditCueItemColor;
		OnRenameCueItemDelegate = InArgs._OnRenameCueItem;
		OnMoveCueItemDelegate = InArgs._OnMoveCueItem;
		OnDeleteCueItemDelegate = InArgs._OnDeleteCueItem;

		SMultiColumnTableRow<TSharedPtr<FDMXControlConsoleEditorCueListItem>>::Construct(
			FSuperRowType::FArguments()
			.OnDragDetected(InArgs._OnDragDetected)
			.OnCanAcceptDrop(InArgs._OnCanAcceptDrop)
			.OnAcceptDrop(InArgs._OnAcceptDrop)
			.IsEnabled(InArgs._IsEnabled)
			.Style(&FDMXEditorStyle::Get().GetWidgetStyle<FTableRowStyle>("FixturePatchList.Row")),
			InOwnerTable);
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueListRow::GenerateWidgetForColumn(const FName& ColumnName)
	{
		if (ColumnName == FDMXControlConsoleEditorCueListColumnIDs::Color)
		{
			return GenerateCueColorRow();
		}
		else if (ColumnName == FDMXControlConsoleEditorCueListColumnIDs::State)
		{
			return GenerateCueStateRow();
		}
		else if (ColumnName == FDMXControlConsoleEditorCueListColumnIDs::Name)
		{
			return GenerateCueNameRow();
		}
		else if (ColumnName == FDMXControlConsoleEditorCueListColumnIDs::Options)
		{
			return GenerateCueOptionsRow();
		}

		return SNullWidget::NullWidget;
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueListRow::GenerateCueColorRow()
	{
		return
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.Padding(5.f, 2.f)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			[
				SNew(SImage)
				.Image(FDMXEditorStyle::Get().GetBrush("DMXEditor.WhiteRoundedPropertyBorder"))
				.ColorAndOpacity(Item.Get(), &FDMXControlConsoleEditorCueListItem::GetCueColor)
				.OnMouseButtonDown(this, &SDMXControlConsoleEditorCueListRow::OnCueColorMouseButtonClick)
			];
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueListRow::GenerateCueStateRow()
	{
		return
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.Padding(5.f, 2.f)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			[
				SNew(SBox)
				.Visibility(this, &SDMXControlConsoleEditorCueListRow::GetRecalledCueTagVisibility)
				.WidthOverride(2.f)
				.Padding(0.f, 10.f)
				[
					SNew(SImage)
					.Image(FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.Rounded.WhiteBrush"))
					.ColorAndOpacity(FLinearColor::White)
				]
			];
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueListRow::GenerateCueNameRow()
	{
		return
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(4.f)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			[
				SAssignNew(CueLabelEditableTextBlock, SInlineEditableTextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(this, &SDMXControlConsoleEditorCueListRow::GetCueNameAsText)
				.ColorAndOpacity(FLinearColor::White)
				.OnTextCommitted(this, &SDMXControlConsoleEditorCueListRow::OnCueNameTextCommitted)
			];
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueListRow::GenerateCueOptionsRow()
	{
		return
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(4.f)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					GenerateRowOptionButtonWidget
					(
						FAppStyle::Get().GetBrush("Icons.Edit"),
						FOnClicked::CreateSP(this, &SDMXControlConsoleEditorCueListRow::OnRenameItemClicked)
					)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					GenerateRowOptionButtonWidget
					(
						FAppStyle::Get().GetBrush("Icons.ChevronUp"),
						FOnClicked::CreateSP(this, &SDMXControlConsoleEditorCueListRow::OnMoveItemClicked, EItemDropZone::AboveItem)
					)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					GenerateRowOptionButtonWidget
					(
						FAppStyle::Get().GetBrush("Icons.ChevronDown"),
						FOnClicked::CreateSP(this, &SDMXControlConsoleEditorCueListRow::OnMoveItemClicked, EItemDropZone::BelowItem)
					)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					GenerateRowOptionButtonWidget
					(
						FAppStyle::Get().GetBrush("Icons.X"),
						FOnClicked::CreateSP(this, &SDMXControlConsoleEditorCueListRow::OnDeleteItemClicked)
					)
				]
			];
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueListRow::GenerateRowOptionButtonWidget(const FSlateBrush* IconBrush, FOnClicked OnClicked)
	{
		return 
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.WidthOverride(22.f)
			.HeightOverride(22.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(OnClicked)
				.ContentPadding(0.f)
				[
					SNew(SImage)
					.Image(IconBrush)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	}

	FReply SDMXControlConsoleEditorCueListRow::OnCueColorMouseButtonClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Item.IsValid())
		{
			return FReply::Unhandled();
		}

		const FLinearColor InitialColor = Item->GetCueColor().GetSpecifiedColor();

		FColorPickerArgs PickerArgs;
		{
			PickerArgs.bOnlyRefreshOnOk = true;
			PickerArgs.bUseAlpha = true;
			PickerArgs.DisplayGamma = TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(GEngine, &UEngine::GetDisplayGamma));
			PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SDMXControlConsoleEditorCueListRow::OnSetCueColorFromColorPicker);
			PickerArgs.InitialColor = InitialColor;
			PickerArgs.ParentWidget = AsShared();
			FWidgetPath ParentWidgetPath;
			if (FSlateApplication::Get().FindPathToWidget(AsShared(), ParentWidgetPath))
			{
				PickerArgs.bOpenAsMenu = FSlateApplication::Get().FindMenuInWidgetPath(ParentWidgetPath).IsValid();
			}
		}

		OpenColorPicker(PickerArgs);

		return FReply::Handled();
	}

	void SDMXControlConsoleEditorCueListRow::OnSetCueColorFromColorPicker(FLinearColor NewColor)
	{
		if (Item.IsValid())
		{
			Item->SetCueColor(NewColor);
			OnEditCueItemColorDelegate.ExecuteIfBound(Item);
		}
	}

	FText SDMXControlConsoleEditorCueListRow::GetCueNameAsText() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const UDMXControlConsoleEditorData* ControlConsoleEditorData = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleEditorData() : nullptr;
		const UDMXControlConsoleCueStack* ControlConsoleCueStack = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleCueStack() : nullptr;
		if (!ControlConsoleEditorData || !ControlConsoleCueStack || !Item.IsValid())
		{
			return FText::GetEmpty();
		}

		const FDMXControlConsoleCue& Cue = Item->GetCue();
		FString CueName = Item->GetCueNameText().ToString();

		// Add 'edited' tag if this is the loaded cue and the control console data are not synched to it
		if (ControlConsoleCueStack->CanStore() && ControlConsoleEditorData->LoadedCue == Cue)
		{
			CueName += TEXT("  [edited]");
		}

		return FText::FromString(CueName);
	}

	void SDMXControlConsoleEditorCueListRow::OnCueNameTextCommitted(const FText& NewName, ETextCommit::Type InCommit)
	{
		if (Item.IsValid())
		{
			Item->SetCueName(NewName.ToString());
			OnRenameCueItemDelegate.ExecuteIfBound(Item);
		}
	}

	void SDMXControlConsoleEditorCueListRow::OnEnterCueLabelTextBlockEditMode()
	{
		EnterCueLabelTextBlockEditModeTimerHandle.Invalidate();

		if (CueLabelEditableTextBlock.IsValid())
		{
			CueLabelEditableTextBlock->EnterEditingMode();
		}
	}

	FReply SDMXControlConsoleEditorCueListRow::OnRenameItemClicked()
	{
		if (!EnterCueLabelTextBlockEditModeTimerHandle.IsValid())
		{
			EnterCueLabelTextBlockEditModeTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &SDMXControlConsoleEditorCueListRow::OnEnterCueLabelTextBlockEditMode));
		}

		return FReply::Handled();
	}

	FReply SDMXControlConsoleEditorCueListRow::OnMoveItemClicked(EItemDropZone DropZone)
	{
		if (Item.IsValid())
		{
			OnMoveCueItemDelegate.ExecuteIfBound(Item, DropZone);
		}

		return FReply::Handled();
	}

	FReply SDMXControlConsoleEditorCueListRow::OnDeleteItemClicked()
	{
		if (Item.IsValid())
		{
			OnDeleteCueItemDelegate.ExecuteIfBound(Item);
		}

		return FReply::Handled();
	}

	EVisibility SDMXControlConsoleEditorCueListRow::GetRecalledCueTagVisibility() const
	{
		bool bIsVisible = false;

		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const UDMXControlConsoleEditorData* ControlConsoleEditorData = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleEditorData() : nullptr;
		if (ControlConsoleEditorData && Item.IsValid())
		{
			bIsVisible = ControlConsoleEditorData->LoadedCue == Item->GetCue();
		}

		return bIsVisible ? EVisibility::Visible : EVisibility::Hidden;
	}
}

#undef LOCTEXT_NAMESPACE
