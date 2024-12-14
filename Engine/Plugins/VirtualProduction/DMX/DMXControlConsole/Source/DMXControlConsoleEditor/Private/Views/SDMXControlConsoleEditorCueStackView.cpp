// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorCueStackView.h"

#include "Algo/AnyOf.h"
#include "DMXControlConsoleCueStack.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorData.h"
#include "Misc/TransactionObjectEvent.h"
#include "Models/DMXControlConsoleCueStackModel.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXControlConsoleEditorCueList.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorCueStackView"

namespace UE::DMX::Private
{
	void SDMXControlConsoleEditorCueStackView::Construct(const FArguments& InArgs, TSharedPtr<FDMXControlConsoleCueStackModel> InCueStackModel)
	{
		if (!ensureMsgf(InCueStackModel.IsValid(), TEXT("Invalid control console cue stack model, can't constuct cue stack view correctly.")))
		{
			return;
		}

		const TSharedRef<FDMXControlConsoleCueStackModel> CueStackModel = InCueStackModel.ToSharedRef();
		WeakCueStackModel = CueStackModel;

		if (UDMXControlConsoleData* ControlConsoleData = WeakCueStackModel.Pin()->GetControlConsoleData())
		{
			ControlConsoleData->GetOnDMXLibraryChanged().AddSP(this, &SDMXControlConsoleEditorCueStackView::OnDMXLibraryChanged);
		}

		ChildSlot
			[
				SNew(SVerticalBox)

				// Cue Stack toolbar section
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.f)
				[
					GenerateCueListToolbar()
				]

				// Cue List View section
				+ SVerticalBox::Slot()
				[
					SAssignNew(CueList, SDMXControlConsoleEditorCueList, CueStackModel)
				]
			];
	}

	bool SDMXControlConsoleEditorCueStackView::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
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

	void SDMXControlConsoleEditorCueStackView::PostUndo(bool bSuccess)
	{
		if (CueList.IsValid())
		{
			CueList->RequestRefresh();
		}
	}

	void SDMXControlConsoleEditorCueStackView::PostRedo(bool bSuccess)
	{
		if (CueList.IsValid())
		{
			CueList->RequestRefresh();
		}
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueStackView::GenerateCueListToolbar()
	{
		const TSharedRef<SWidget> CueListToolbar =
			SNew(SHorizontalBox)

			// Add New Cue button section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
				.ForegroundColor(FSlateColor::UseStyle())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(this, &SDMXControlConsoleEditorCueStackView::OnAddNewCueClicked)
				.IsEnabled(this, &SDMXControlConsoleEditorCueStackView::IsAddNewCueButtonEnabled)
				.ContentPadding(FMargin(0.f, 4.f))
				[
					GenerateCueListToolbarButtonContent
					(
						LOCTEXT("AddNewCueButton_Label", "Add New"),
						LOCTEXT("AddNewCueButton_ToolTip", "Add a new cue based on the current state of the  control console."),
						FAppStyle::Get().GetBrush("Icons.Plus"),
						FStyleColors::AccentGreen
					)
				]
			]

			// Store Cue button section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
				.ForegroundColor(FSlateColor::UseStyle())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(this, &SDMXControlConsoleEditorCueStackView::OnStoreCueClicked)
				.IsEnabled(this, &SDMXControlConsoleEditorCueStackView::IsStoreCueButtonEnabled)
				.ContentPadding(FMargin(0.f, 4.f))
				[
					GenerateCueListToolbarButtonContent
					(
						LOCTEXT("StoreCueButton_Label", "Store"),
						LOCTEXT("StoreCueButton_ToolTip", "Stores the current state of te console in the selected cue."),
						FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.StoreCue"),
						FStyleColors::White
					)
				]
			]

			// Recall Cue button section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
				.ForegroundColor(FSlateColor::UseStyle())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(this, &SDMXControlConsoleEditorCueStackView::OnRecallCueClicked)
				.IsEnabled(this, &SDMXControlConsoleEditorCueStackView::IsRecallCueButtonEnabled)
				.ContentPadding(FMargin(0.f, 4.f))
				[
					GenerateCueListToolbarButtonContent
					(
						LOCTEXT("RecallCueButton_Label", "Recall"),
						LOCTEXT("RecallCueButton_ToolTip", "Recalls the selected cue loding its data to the console."),
						FAppStyle::Get().GetBrush("Icons.SortUp"),
						FStyleColors::White
					)
				]
			]

			// Clear Stack button section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.f)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
				.ForegroundColor(FSlateColor::UseStyle())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(this, &SDMXControlConsoleEditorCueStackView::OnClearCueStackClicked)
				.IsEnabled(this, &SDMXControlConsoleEditorCueStackView::IsClearAllCuesButtonEnabled)
				.ContentPadding(FMargin(0.f, 4.f))
				[
					GenerateCueListToolbarButtonContent
					(
						LOCTEXT("ClearAllCuesButton_Label", "Clear"),
						LOCTEXT("ClearAllCuesButton_ToolTip", "Clear all the cues in the stack."),
						FAppStyle::Get().GetBrush("Icons.Delete"),
						FStyleColors::White
					)
				]
			];

		return CueListToolbar;
	}

	TSharedRef<SWidget> SDMXControlConsoleEditorCueStackView::GenerateCueListToolbarButtonContent(const FText& Label, const FText& ToolTip, const FSlateBrush* IconBrush, const FSlateColor IconColor)
	{
		const TSharedRef<SWidget> CueListToolbarButtonContent =
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(IconBrush)
				.ColorAndOpacity(IconColor)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(Label)
				.ToolTipText(ToolTip)
				.TextStyle(FAppStyle::Get(), "SmallButtonText")
			];

		return CueListToolbarButtonContent;
	}

	bool SDMXControlConsoleEditorCueStackView::IsAddNewCueButtonEnabled() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		return CueStackModel.IsValid() && CueStackModel->IsAddNewCueButtonEnabled();
	}

	bool SDMXControlConsoleEditorCueStackView::IsStoreCueButtonEnabled() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (!CueStackModel.IsValid() || !CueList.IsValid())
		{
			return false;
		}

		const TSharedPtr<FDMXControlConsoleEditorCueListItem> SelectedCueItem = !CueList->GetSelectedCueItems().IsEmpty() ? CueList->GetSelectedCueItems()[0] : nullptr;
		if(!SelectedCueItem.IsValid())
		{
			return false;
		}
		
		const FDMXControlConsoleCue& SelectedCue = SelectedCueItem->GetCue();
		return CueStackModel->IsStoreCueButtonEnabled(SelectedCue);
	}

	bool SDMXControlConsoleEditorCueStackView::IsRecallCueButtonEnabled() const
	{
		return CueList.IsValid() && !CueList->GetSelectedCueItems().IsEmpty();
	}

	bool SDMXControlConsoleEditorCueStackView::IsClearAllCuesButtonEnabled() const
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		const UDMXControlConsoleCueStack* ControlConsoleCueStack = CueStackModel.IsValid() ? CueStackModel->GetControlConsoleCueStack() : nullptr;
		return ControlConsoleCueStack && !ControlConsoleCueStack->GetCuesArray().IsEmpty();
	}

	FReply SDMXControlConsoleEditorCueStackView::OnAddNewCueClicked()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (CueStackModel.IsValid())
		{
			CueStackModel->AddNewCue();

			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	FReply SDMXControlConsoleEditorCueStackView::OnStoreCueClicked()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (!CueStackModel.IsValid() || !CueList.IsValid())
		{
			return FReply::Unhandled();
		}

		const TArray<TSharedPtr<FDMXControlConsoleEditorCueListItem>> SelectedItems = CueList->GetSelectedCueItems();
		if (SelectedItems.IsEmpty())
		{
			return FReply::Unhandled();
		}

		const TSharedPtr<FDMXControlConsoleEditorCueListItem> SelectedItem = SelectedItems[0];
		if (!SelectedItem.IsValid())
		{
			return FReply::Unhandled();
		}

		const FDMXControlConsoleCue& SelectedCue = SelectedItem->GetCue();
		CueStackModel->StoreCue(SelectedCue);

		return FReply::Handled();
	}

	FReply SDMXControlConsoleEditorCueStackView::OnRecallCueClicked()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (!CueStackModel.IsValid() || !CueList.IsValid())
		{
			return FReply::Unhandled();
		}

		const TArray<TSharedPtr<FDMXControlConsoleEditorCueListItem>> SelectedItems = CueList->GetSelectedCueItems();
		if (SelectedItems.IsEmpty())
		{
			return FReply::Unhandled();
		}

		const TSharedPtr<FDMXControlConsoleEditorCueListItem> SelectedItem = SelectedItems[0];
		if (!SelectedItem.IsValid())
		{
			return FReply::Unhandled();
		}

		const FDMXControlConsoleCue& SelectedCue = SelectedItem->GetCue();
		CueStackModel->RecallCue(SelectedCue);

		return FReply::Handled();
	}

	FReply SDMXControlConsoleEditorCueStackView::OnClearCueStackClicked()
	{
		const TSharedPtr<FDMXControlConsoleCueStackModel> CueStackModel = WeakCueStackModel.Pin();
		if (CueStackModel.IsValid())
		{
			CueStackModel->ClearCueStack();

			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	void SDMXControlConsoleEditorCueStackView::OnDMXLibraryChanged()
	{
		OnClearCueStackClicked();
	}
}

#undef LOCTEXT_NAMESPACE
