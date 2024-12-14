// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaEaseCurvePreset.h"
#include "EaseCurveTool/AvaEaseCurvePreset.h"
#include "EaseCurveTool/AvaEaseCurveStyle.h"
#include "EaseCurveTool/AvaEaseCurveSubsystem.h"
#include "Editor.h"
#include "Framework/SlateDelegates.h"
#include "Internationalization/Text.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformWrapPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "SAvaEaseCurvePreset"

void SAvaEaseCurvePreset::Construct(const FArguments& InArgs)
{
	DisplayRate = InArgs._DisplayRate;
	OnPresetChanged = InArgs._OnPresetChanged;
	OnQuickPresetChanged = InArgs._OnQuickPresetChanged;
	OnGetNewPresetTangents = InArgs._OnGetNewPresetTangents;

	ChildSlot
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]()
				{
					return bIsCreatingNewPreset ? 1 : 0;
				})
			+ SWidgetSwitcher::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SAssignNew(PresetComboBox, SAvaEaseCurvePresetComboBox)
					.DisplayRate(DisplayRate)
					.AllowEditMode(true)
					.OnPresetChanged(OnPresetChanged)
					.OnQuickPresetChanged(OnQuickPresetChanged)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton"))
					.VAlign(VAlign_Center)
					.ToolTipText(LOCTEXT("AddNewPresetToolTip", "Save the current ease curve as a new preset"))
					.IsEnabled_Lambda([this]()
						{
							return !PresetComboBox->HasSelection();
						})
					.OnClicked(this, &SAvaEaseCurvePreset::OnCreateNewPresetClick)
					[
						SNew(SImage)
						.DesiredSizeOverride(FVector2D(FAvaEaseCurveStyle::Get().GetFloat(TEXT("ToolButton.ImageSize"))))
						.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
					]
				]
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SAssignNew(NewPresetNameTextBox, SEditableTextBox)
					.OnKeyDownHandler(this, &SAvaEaseCurvePreset::OnNewPresetKeyDownHandler)
					.OnTextCommitted(this, &SAvaEaseCurvePreset::OnNewPresetTextCommitted)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAvaEaseCurveStyle::Get(), TEXT("ToolButton"))
					.VAlign(VAlign_Center)
					.ToolTipText(LOCTEXT("CancelNewPresetToolTip", "Cancels the current new ease curve preset operation"))
					.IsEnabled_Lambda([this]()
						{
							return !PresetComboBox->HasSelection();
						})
					.OnClicked(this, &SAvaEaseCurvePreset::OnCancelNewPresetClick)
					[
						SNew(SImage)
						.DesiredSizeOverride(FVector2D(FAvaEaseCurveStyle::Get().GetFloat(TEXT("ToolButton.ImageSize"))))
						.Image(FAppStyle::GetBrush(TEXT("Icons.X")))
					]
				]
			]
		];
}

FReply SAvaEaseCurvePreset::OnCreateNewPresetClick()
{
	bIsCreatingNewPreset = true;

	FSlateApplication::Get().SetAllUserFocus(NewPresetNameTextBox);

	return FReply::Handled();
}

FReply SAvaEaseCurvePreset::OnCancelNewPresetClick()
{
	bIsCreatingNewPreset = false;
	NewPresetNameTextBox->SetText(FText());

	return FReply::Handled();
}

FReply SAvaEaseCurvePreset::OnNewPresetKeyDownHandler(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		bIsCreatingNewPreset = false;
		NewPresetNameTextBox->SetText(FText());

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAvaEaseCurvePreset::OnNewPresetTextCommitted(const FText& InNewText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnEnter && !InNewText.IsEmpty())
	{
		FAvaEaseCurveTangents NewTangents;
		if (OnGetNewPresetTangents.IsBound() && OnGetNewPresetTangents.Execute(NewTangents))
		{
			UAvaEaseCurveSubsystem& EaseCurveSubsystem = UAvaEaseCurveSubsystem::Get();

			const TSharedPtr<FAvaEaseCurvePreset> NewPreset = EaseCurveSubsystem.AddPreset(InNewText.ToString(), NewTangents);
			if (NewPreset.IsValid())
			{
				PresetComboBox->SetSelectedItem(*NewPreset);
			}
		}
	}

	bIsCreatingNewPreset = false;
	NewPresetNameTextBox->SetText(FText());
}

void SAvaEaseCurvePreset::ClearSelection()
{
	PresetComboBox->ClearSelection();
}

bool SAvaEaseCurvePreset::SetSelectedItem(const FString& InName)
{
	return PresetComboBox->SetSelectedItem(InName);
}

bool SAvaEaseCurvePreset::SetSelectedItem(const FAvaEaseCurveTangents& InTangents)
{
	return PresetComboBox->SetSelectedItem(InTangents);
}

bool SAvaEaseCurvePreset::SetSelectedItem(const FAvaEaseCurvePreset& InPreset)
{
	return SetSelectedItem(InPreset.Name);
}

#undef LOCTEXT_NAMESPACE
