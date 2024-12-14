// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorCueStackComboBox.h"

#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "DMXControlConsoleCueStack.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorData.h"
#include "DMXEditorStyle.h"
#include "Misc/TransactionObjectEvent.h"
#include "Models/DMXControlConsoleCueStackModel.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXControlConsoleEditorCueList.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorCueStackComboBox"

namespace UE::DMX::Private
{
	void SDMXControlConsoleEditorCueStackComboBox::Construct(const FArguments& InArgs, TSharedPtr<FDMXControlConsoleCueStackModel> InCueStackModel)
	{
		if (!ensureMsgf(InCueStackModel.IsValid(), TEXT("Invalid control console cue stack model, cannot create layout toolbar correctly.")))
		{
			return;
		}

		WeakCueStackModel = InCueStackModel;

		const UDMXControlConsoleData* ControlConsoleData = InCueStackModel->GetControlConsoleData();
		UDMXControlConsoleCueStack* ControlConsoleCueStack = ControlConsoleData ? ControlConsoleData->GetCueStack() : nullptr;
		if (ControlConsoleCueStack)
		{
			ControlConsoleCueStack->GetOnCueStackChanged().AddSP(this, &SDMXControlConsoleEditorCueStackComboBox::UpdateCueStackComboBoxSource);
		}

		ChildSlot
			[
				SNew(SHorizontalBox)

				// Cue stack combo box section
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SAssignNew(CueStackComboBox, SComboBox<TSharedPtr<FDMXControlConsoleEditorCueListItem>>)
					.OnGenerateWidget(this, &SDMXControlConsoleEditorCueStackComboBox::GenerateComboBoxOptionWidget)
					.OptionsSource(&ComboBoxSource)
					.OnSelectionChanged(this, &SDMXControlConsoleEditorCueStackComboBox::OnCueStackComboBoxSelectionChanged)
					.ComboBoxStyle(&FAppStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox")))
					.ItemStyle(&FDMXControlConsoleEditorStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("DMXControlConsole.FaderGroupToolbar")))
					[
						GenerateComboBoxContentWidget()
					]
				]

				// Add new cue button section
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SBox)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.WidthOverride(28.f)
					.HeightOverride(22.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "Button")
						.ForegroundColor(FSlateColor::UseForeground())
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnClicked(this, &SDMXControlConsoleEditorCueStackComboBox::OnAddNewCueClicked)
						.IsEnabled(this, &SDMXControlConsoleEditorCueStackComboBox::IsAddNewCueButtonEnabled)
						.ToolTipText(LOCTEXT("CueStackComboBoxAddNewCueButton_ToolTip", "Add New Cue"))
						.ContentPadding(FMargin(-10.f, 0.f))
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("Icons.Plus"))
							.ColorAndOpacity(FStyleColors::AccentGreen)
						]
					]
				]

				// Store cue button section
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SBox)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.WidthOverride(28.f)
					.HeightOverride(22.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "Button")
						.ForegroundColor(FSlateColor::UseForeground())
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnClicked(this, &SDMXControlConsoleEditorCueStackComboBox::OnStoreCueClicked)
						.IsEnabled(this, &SDMXControlConsoleEditorCueStackComboBox::IsStoreCueButtonEnabled)
						.ToolTipText(LOCTEXT("CueStackComboBoxStoreCueButton_ToolTip", "Store Cue"))
						.ContentPadding(FMargin(-10.f, 0.f))
						[
							SNew(SImage)
							.Image(FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.StoreCue"))
							.ColorAndOpacity(FStyleColors::White)
						]
					]
				]
			];

		UpdateCueStackComboBoxSource();
	}

	bool SDMXControlConsoleEditorCueStackComboBox::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
	{
		const TArray<UClass*> MatchingContextClasses =
		{
			UDMXControlConsoleData::StaticClass(),
			UDMXControlConsoleCueStack::StaticClass()
		};

		const bool bMatchesContext = Algo::AnyOf(TransactionObjectContexts,
			[this, MatchingContextClasses](const TPair<UObject*, FTransactionObjectEvent>& Pair)
			{
				bool bMatchesClasses = false;
				const UObject* Object = Pair.Key;
				if (IsValid(Object))
				{
					const UClass* ObjectClass = Object->GetClass();
					bMatchesClasses = Algo::AnyOf(MatchingContextClasses, [ObjectClass](UClass* InClass)
						{
							return IsValid(ObjectClass) && ObjectClass->IsChildOf(InClass);
						});
				}

				return bMatchesClasses;
			});

		return bMatchesContext;
	}

	void SDMXControlConsoleEditorCueStackComboBox::PostUndo(bool bSuccess)
	{
		UpdateCueStackComboBoxSource();
	}

	void SDMXControlConsoleEditorCueStackComboBox::PostRedo(bool bSuccess)
	{
		UpdateCueStackComboBoxSource();
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueStackComboBox::GenerateComboBoxContentWidget()
	{
		const TSharedRef<SWidget> ComboBoxContentWidget =
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(4.f)
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.MinDesiredWidth(4.f)
				.MinDesiredHeight(14.f)
				[
					SNew(SImage)
					.Image(FDMXEditorStyle::Get().GetBrush("DMXEditor.WhiteRoundedPropertyBorder"))
					.ColorAndOpacity(this, &SDMXControlConsoleEditorCueStackComboBox::GetLoadedCueColor)
				]
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f)
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(70.f)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(this, &SDMXControlConsoleEditorCueStackComboBox::GetLoadedCueNameAsText)
				]
			];

		return ComboBoxContentWidget;
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueStackComboBox::GenerateComboBoxOptionWidget(const TSharedPtr<FDMXControlConsoleEditorCueListItem> CueItem)
	{
		if (!CueItem.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		const FSlateColor CueColor = CueItem->GetCueColor();
		const FText CueNameAsText =  CueItem->GetCueNameText();

		const TSharedRef<SWidget> ComboBoxOptionWidget =
			SNew(SHorizontalBox)

			// Row color tag
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.MinDesiredWidth(4.f)
				.MinDesiredHeight(14.f)
				[
					SNew(SImage)
					.Image(FDMXEditorStyle::Get().GetBrush("DMXEditor.WhiteRoundedPropertyBorder"))
					.ColorAndOpacity(CueColor)
				]
			]

			// Row name label
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.MaxWidth(140.f)
			.Padding(6.f, 0.f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(CueNameAsText)
				.ToolTipText(CueNameAsText)
			];

			return ComboBoxOptionWidget;
	}

	void SDMXControlConsoleEditorCueStackComboBox::UpdateCueStackComboBoxSource()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const UDMXControlConsoleEditorData* ControlConsoleEditorData = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleEditorData() : nullptr;
		const UDMXControlConsoleCueStack* ControlConsoleCueStack = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleCueStack() : nullptr;
		if (!ControlConsoleEditorData || !ControlConsoleCueStack || !CueStackComboBox.IsValid())
		{
			return;
		}

		ComboBoxSource.Reset();

		TSharedPtr<FDMXControlConsoleEditorCueListItem> LastLoadedCueItem;
		const TArray<FDMXControlConsoleCue>& CuesArray = ControlConsoleCueStack->GetCuesArray();
		for (const FDMXControlConsoleCue& Cue : CuesArray)
		{
			const TSharedRef<FDMXControlConsoleEditorCueListItem> CueListItem = MakeShared<FDMXControlConsoleEditorCueListItem>(Cue);
			if (Cue == ControlConsoleEditorData->LoadedCue)
			{
				LastLoadedCueItem = CueListItem;
			}

			ComboBoxSource.Add(CueListItem);
		}

		CueStackComboBox->RefreshOptions();
		if (LastLoadedCueItem.IsValid())
		{
			CueStackComboBox->SetSelectedItem(LastLoadedCueItem);
		}
	}

	void SDMXControlConsoleEditorCueStackComboBox::OnCueStackComboBoxSelectionChanged(const TSharedPtr<FDMXControlConsoleEditorCueListItem> NewSelection, ESelectInfo::Type SelectInfo)
	{
		if (!NewSelection.IsValid() || (SelectInfo != ESelectInfo::OnMouseClick && SelectInfo != ESelectInfo::OnKeyPress))
		{
			return;
		}

		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (CueStackModel.IsValid())
		{
			const FDMXControlConsoleCue& SelectedCue = NewSelection->GetCue();
			CueStackModel->RecallCue(SelectedCue);
		}
	}

	bool SDMXControlConsoleEditorCueStackComboBox::IsAddNewCueButtonEnabled() const
	{
		return WeakCueStackModel.IsValid() && WeakCueStackModel.Pin()->IsAddNewCueButtonEnabled();
	}

	bool SDMXControlConsoleEditorCueStackComboBox::IsStoreCueButtonEnabled() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const TSharedPtr<FDMXControlConsoleEditorCueListItem> SelectedCueItem = CueStackComboBox.IsValid() ? CueStackComboBox->GetSelectedItem() : nullptr;
		if (!CueStackModel.IsValid() || !SelectedCueItem.IsValid())
		{
			return false;
		}
		
		const FDMXControlConsoleCue& SelectedCue = SelectedCueItem->GetCue();
		return CueStackModel->IsStoreCueButtonEnabled(SelectedCue);
	}

	FReply SDMXControlConsoleEditorCueStackComboBox::OnAddNewCueClicked()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (CueStackModel.IsValid())
		{
			CueStackModel->AddNewCue();

			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	FReply SDMXControlConsoleEditorCueStackComboBox::OnStoreCueClicked()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (!CueStackModel.IsValid() || !CueStackComboBox.IsValid())
		{
			return FReply::Unhandled();
		}

		const TSharedPtr<FDMXControlConsoleEditorCueListItem> SelectedItem = CueStackComboBox->GetSelectedItem();
		if (!SelectedItem.IsValid())
		{
			return FReply::Unhandled();
		}

		const FDMXControlConsoleCue& SelectedCue = SelectedItem->GetCue();
		CueStackModel->StoreCue(SelectedCue);

		return FReply::Handled();
	}

	FSlateColor SDMXControlConsoleEditorCueStackComboBox::GetLoadedCueColor() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const UDMXControlConsoleEditorData* ControlConsoleEditorData = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleEditorData() : nullptr;
		UDMXControlConsoleCueStack* ControlConsoleCueStack = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleCueStack() : nullptr;
		const bool bHasValidCueData =
			ControlConsoleEditorData &&
			ControlConsoleCueStack &&
			ControlConsoleCueStack->FindCue(ControlConsoleEditorData->LoadedCue.CueID);

		return bHasValidCueData ? ControlConsoleEditorData->LoadedCue.CueColor : FLinearColor::White;
	}

	FText SDMXControlConsoleEditorCueStackComboBox::GetLoadedCueNameAsText() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const UDMXControlConsoleEditorData* ControlConsoleEditorData = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleEditorData() : nullptr;
		UDMXControlConsoleCueStack* ControlConsoleCueStack = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleCueStack() : nullptr;
		const bool bHasValidCueData =
			ControlConsoleEditorData &&
			ControlConsoleCueStack &&
			ControlConsoleCueStack->FindCue(ControlConsoleEditorData->LoadedCue.CueID);

		if (!bHasValidCueData)
		{
			return LOCTEXT("NoValidCueText", "No Cue");
		}

		FString LastRecalledCueName = ControlConsoleEditorData->LoadedCue.CueLabel;

		// Add 'edited' tag if the control console data are not synched to the loaded cue
		if (ControlConsoleCueStack->CanStore())
		{
			LastRecalledCueName += TEXT("  [edited]");
		}

		return FText::FromString(LastRecalledCueName);
	}
}

#undef LOCTEXT_NAMESPACE
