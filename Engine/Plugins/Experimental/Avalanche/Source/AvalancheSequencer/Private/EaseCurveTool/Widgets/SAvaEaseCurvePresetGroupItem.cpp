// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaEaseCurvePresetGroupItem.h"
#include "EaseCurveTool/AvaEaseCurvePreset.h"
#include "EaseCurveTool/AvaEaseCurvePresetDragDropOp.h"
#include "EaseCurveTool/AvaEaseCurveStyle.h"
#include "EaseCurveTool/AvaEaseCurveToolCommands.h"
#include "EaseCurveTool/AvaEaseCurveToolSettings.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePreview.h"
#include "Internationalization/Text.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "SAvaEaseCurvePresetGroupItem"

void SAvaEaseCurvePresetGroupItem::Construct(const FArguments& InArgs, const TSharedPtr<STableViewBase>& InOwnerTableView)
{
	Preset = InArgs._Preset;
	bIsEditMode = InArgs._IsEditMode;
	IsSelected = InArgs._IsSelected;
	OnDelete = InArgs._OnDelete;
	OnRename = InArgs._OnRename;
	OnBeginMove = InArgs._OnBeginMove;
	OnEndMove = InArgs._OnEndMove;
	OnClick = InArgs._OnClick;
	OnSetQuickEase = InArgs._OnSetQuickEase;

	const FText ItemTooltipText = FText::Format(LOCTEXT("ItemTooltip", "{0}\n\nShift + Click to set as active quick preset"), FText::FromString(Preset->Name));

	ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.Visibility(this, &SAvaEaseCurvePresetGroupItem::GetBorderVisibility)
				.BorderImage(this, &SAvaEaseCurvePresetGroupItem::GetBackgroundImage)
			]
			+ SOverlay::Slot()
			.Padding(1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(3.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(160)
					.Clipping(EWidgetClipping::ClipToBoundsAlways)
					[
						SNew(SWidgetSwitcher)
						.WidgetIndex_Lambda([this]()
							{
								return IsEditMode() ? 1 : 0;
							})
						// Normal mode
						+ SWidgetSwitcher::Slot()
						[
							SNew(STextBlock)
							.TextStyle(FAppStyle::Get(), TEXT("Menu.Label"))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.ColorAndOpacity(FStyleColors::White)
							.Text(FText::FromString(Preset->Name))
							.ToolTipText(ItemTooltipText)
						]
						// Edit mode
						+ SWidgetSwitcher::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton.NoPad"))
								.VAlign(VAlign_Center)
								.ToolTipText(LOCTEXT("EditModeDeleteTooltip", "Delete this category and the json file associated with it on disk"))
								.Visibility(this, &SAvaEaseCurvePresetGroupItem::GetEditModeVisibility)
								.OnClicked(this, &SAvaEaseCurvePresetGroupItem::HandleDeleteClick)
								[
									SNew(SImage)
									.DesiredSizeOverride(FVector2D(10.f))
									.Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))
								]
							]
							+ SHorizontalBox::Slot()
							.Padding(2.f, 0.f, 0.f, 0.f)
							[
								SAssignNew(RenameTextBox, SEditableTextBox)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text(FText::FromString(Preset->Name))
								.ToolTipText(FText::FromString(Preset->Name))
								.OnTextCommitted(this, &SAvaEaseCurvePresetGroupItem::HandleRenameTextCommitted)
							]
						]
					]
				]
				// Quick Ease Button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SBorder)
					.Padding(0.f)
					.BorderImage(FAvaEaseCurveStyle::Get().GetBrush(TEXT("ToolButton.Opaque")))
					[
						SNew(SButton)
						.VAlign(VAlign_Center)
						.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton.NoPad"))
						.ToolTipText(this, &SAvaEaseCurvePresetGroupItem::GetQuickPresetIconToolTip)
						.Visibility(this, &SAvaEaseCurvePresetGroupItem::GetQuickPresetIconVisibility)
						.OnClicked(this, &SAvaEaseCurvePresetGroupItem::HandleSetQuickEase)
						[
							SNew(SImage)
							.DesiredSizeOverride(FVector2D(10.f))
							.Image(FAppStyle::GetBrush(TEXT("Icons.Adjust")))
							.ColorAndOpacity(this, &SAvaEaseCurvePresetGroupItem::GetQuickPresetIconColor)
						]
					]
				]
				// Preview Image
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SBorder)
					.BorderBackgroundColor(FStyleColors::White25)
					.Padding(2.f)
					.OnMouseButtonDown(this, &SAvaEaseCurvePresetGroupItem::OnMouseButtonDown)
					[
						SNew(SAvaEaseCurvePreview)
						.PreviewSize(20.f)
						.CurveThickness(1.5f)
						.Tangents(Preset->Tangents)
						.CustomToolTip(true)
						.BackgroundColor(FStyleColors::Dropdown.GetSpecifiedColor())
						.UnderCurveColor(FStyleColors::SelectInactive.GetSpecifiedColor())
						.DisplayRate(InArgs._DisplayRate)
					]
				]
			]
		];

	STableRow<TSharedPtr<FAvaEaseCurvePreset>>::ConstructInternal(
		STableRow::FArguments()
		.Style(&FAppStyle::GetWidgetStyle<FTableRowStyle>(TEXT("ComboBox.Row")))
		.Padding(5.f)
		.ShowSelection(true)
		, InOwnerTableView.ToSharedRef());
}

void SAvaEaseCurvePresetGroupItem::SetPreset(const TSharedPtr<FAvaEaseCurvePreset>& InPreset)
{
	Preset = InPreset;
}

bool SAvaEaseCurvePresetGroupItem::IsEditMode() const
{
	return bIsEditMode.Get(false);
}

EVisibility SAvaEaseCurvePresetGroupItem::GetEditModeVisibility() const
{
	return IsEditMode() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SAvaEaseCurvePresetGroupItem::GetBorderVisibility() const
{
	return ((bIsDragging && IsEditMode()) || (IsSelected.IsSet() && IsSelected.Get()))
		? EVisibility::Visible: EVisibility::Collapsed;
}

const FSlateBrush* SAvaEaseCurvePresetGroupItem::GetBackgroundImage() const
{
	FName BrushName = NAME_None;

	if (bIsDragging)
	{
		BrushName = TEXT("EditMode.Background.Over");
	}
	else if (IsSelected.IsSet() && IsSelected.Get())
	{
		BrushName = TEXT("Preset.Selected");
	}

	return BrushName.IsNone() ? nullptr : FAvaEaseCurveStyle::Get().GetBrush(BrushName);
}

void SAvaEaseCurvePresetGroupItem::HandleRenameTextCommitted(const FText& InNewText, ETextCommit::Type InCommitType) const
{
	const FString NewPresetName = InNewText.ToString();
	
	if (!NewPresetName.IsEmpty() && !NewPresetName.Equals(Preset->Name)
		&& OnRename.IsBound() && OnRename.Execute(Preset, NewPresetName))
	{
		Preset->Name = NewPresetName;
	}
	else
	{
		RenameTextBox->SetText(FText::FromString(Preset->Name));
	}
}

FReply SAvaEaseCurvePresetGroupItem::HandleDeleteClick() const
{
	if (OnDelete.IsBound())
	{
		OnDelete.Execute(Preset);
	}

	return FReply::Handled();
}

FReply SAvaEaseCurvePresetGroupItem::OnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsEditMode())
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}

	if (OnClick.IsBound())
	{
		OnClick.Execute(Preset);
	}

	return FReply::Handled();
}

FReply SAvaEaseCurvePresetGroupItem::OnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	TriggerEndMove();

	return STableRow::OnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply SAvaEaseCurvePresetGroupItem::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!IsEditMode())
	{
		return FReply::Unhandled();
	}

	TriggerBeginMove();

	const TSharedRef<FAvaEaseCurvePresetDragDropOperation> Operation = MakeShared<FAvaEaseCurvePresetDragDropOperation>(SharedThis(this), Preset);
	return FReply::Handled().BeginDragDrop(Operation);
}

FReply SAvaEaseCurvePresetGroupItem::OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent)
{
	bIsDragging = false;

	return STableRow::OnDrop(InGeometry, InDragDropEvent);
}

void SAvaEaseCurvePresetGroupItem::TriggerBeginMove()
{
	if (OnBeginMove.IsBound())
	{
		OnBeginMove.Execute(Preset, Preset->Category);
	}

	bIsDragging = true;
}

void SAvaEaseCurvePresetGroupItem::TriggerEndMove()
{
	if (OnEndMove.IsBound())
	{
		OnEndMove.Execute(Preset, Preset->Category);
	}

	bIsDragging = false;
}

FSlateColor SAvaEaseCurvePresetGroupItem::GetQuickPresetIconColor() const
{
	return (IsHovered() && IsQuickEasePreset()) ? FStyleColors::Select : FSlateColor::UseStyle();
}

EVisibility SAvaEaseCurvePresetGroupItem::GetQuickPresetIconVisibility() const
{
	return (IsHovered() || IsQuickEasePreset()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SAvaEaseCurvePresetGroupItem::GetQuickPresetIconToolTip() const
{
	static const FText QuickEaseText = IsQuickEasePreset()
		? LOCTEXT("ActiveQuickEaseIconTooltip", "Active Quick Ease Preset")
		: LOCTEXT("QuickEaseIconTooltip", "Set to Active Quick Ease Preset");

	const FAvaEaseCurveToolCommands& EaseCurveToolCommands = FAvaEaseCurveToolCommands::Get();

	FText CommandText;

	if (EaseCurveToolCommands.QuickEase->GetFirstValidChord()->IsValidChord())
	{
		CommandText = FText::Format(LOCTEXT("QuickEaseIconInOutTooltip", "{0}{1} - Apply to Out (Leave) and In (Arrive) tangents\n")
			, CommandText, EaseCurveToolCommands.QuickEase->GetInputText());
	}

	if (EaseCurveToolCommands.QuickEaseIn->GetFirstValidChord()->IsValidChord())
	{
		CommandText = FText::Format(LOCTEXT("QuickEaseIconInTooltip", "{0}{1} - Apply to In (Arrive) tangent only\n")
			, CommandText, EaseCurveToolCommands.QuickEaseIn->GetInputText());
	}

	if (EaseCurveToolCommands.QuickEaseOut->GetFirstValidChord()->IsValidChord())
	{
		CommandText = FText::Format(LOCTEXT("QuickEaseIconOutTooltip", "{0}{1} - Apply to Out (Leave) tangent only\n")
			, CommandText, EaseCurveToolCommands.QuickEaseOut->GetInputText());
	}

	return CommandText.IsEmpty() ? QuickEaseText : FText::Format(LOCTEXT("QuickEasePresetIconTooltip", "{0}\n\n{1}"), QuickEaseText, CommandText);
}

FReply SAvaEaseCurvePresetGroupItem::HandleSetQuickEase()
{
	if (OnSetQuickEase.IsBound())
	{
		OnSetQuickEase.Execute(Preset);
	}

	return FReply::Handled();
}

bool SAvaEaseCurvePresetGroupItem::IsQuickEasePreset() const
{
	if (!Preset.IsValid())
	{
		return false;
	}
	
	const UAvaEaseCurveToolSettings* const Settings = GetDefault<UAvaEaseCurveToolSettings>();
	check(IsValid(Settings));
	
	FAvaEaseCurveTangents Tangents;
	
	if (!FAvaEaseCurveTangents::FromString(Settings->GetQuickEaseTangents(), Tangents))
	{
		return false;
	}

	return Tangents.IsNearlyEqual(Preset->Tangents);
}

#undef LOCTEXT_NAMESPACE
