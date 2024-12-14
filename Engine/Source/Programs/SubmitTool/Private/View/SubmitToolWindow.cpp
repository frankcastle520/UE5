// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolWindow.h"
#include "HAL/FileManager.h"
#include "Widgets/AutoUpdateWidget.h"
#include "Widgets/SubmitToolWidget.h"
#include "Widgets/SWindow.h"
#include "Models/ModelInterface.h"
#include "Models/SubmitToolUserPrefs.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"

SubmitToolWindow::SubmitToolWindow(FModelInterface* modelInterface) : ModelInterface(modelInterface)
{}

TSharedRef<SDockTab> SubmitToolWindow::BuildMainTab(TSharedPtr<SWindow> InParentWindow)
{
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Create a submit files window
	MainTab = SNew(SDockTab)
		.TabRole(ETabRole::MajorTab)
		.CanEverClose(false)
		.OnCanCloseTab(SDockTab::FCanCloseTab::CreateRaw(this, &SubmitToolWindow::OnCanCloseTab))
		.OnTabClosed_Lambda([](TSharedRef<SDockTab> Tab){
							if(!Tab->GetParentWindow()->IsWindowMaximized())
							{
								FSubmitToolUserPrefs::Get()->WindowPosition = Tab->GetParentWindow()->GetPositionInScreen();
								FSubmitToolUserPrefs::Get()->WindowSize = Tab->GetParentWindow()->GetSizeInScreen();
							}
						})
		.Label_Lambda([&ModelInterface = ModelInterface]() {return FText::FromString(FString::Printf(TEXT("Changelist: %s"), *ModelInterface->GetCLID())); });


	if (ModelInterface->CheckForNewVersion())
	{
		CreateAutoUpdateSubmitToolContent(InParentWindow);
	}
	else
	{
		CreateMainSubmitToolContent(InParentWindow);
	}

	return MainTab.ToSharedRef();
}

void SubmitToolWindow::CreateAutoUpdateSubmitToolContent(TSharedPtr<SWindow> InParentWindow)
{
	TSharedRef<SAutoUpdateWidget> AutoUpdateWidget =
		SNew(SAutoUpdateWidget)
		.ModelInterface(ModelInterface)
		.OnAutoUpdateCancelled_Lambda([InParentWindow, this]() { CreateMainSubmitToolContent(InParentWindow); });

	ModelInterface->SetMainTab(MainTab);
	MainTab->SetContent(AutoUpdateWidget);
}

void SubmitToolWindow::CreateMainSubmitToolContent(TSharedPtr<SWindow> InParentWindow)
{
	TSharedRef<SubmitToolWidget> SourceControlWidget =
		SNew(SubmitToolWidget)
		.ParentWindow(InParentWindow)
		.ParentTab(MainTab)
		.ModelInterface(ModelInterface);

	ModelInterface->SetMainTab(MainTab);
	MainTab->SetContent(SourceControlWidget);
}

bool SubmitToolWindow::OnCanCloseTab()
{
	if (ModelInterface->IsP4OperationRunning())
	{
		TSharedPtr<SWindow> WaitForP4Dialog = SNew(SWindow)
			.Title(FText::FromString("Closing requested"))
			.SizingRule(ESizingRule::Autosized)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.HasCloseButton(false);

		WaitForP4Dialog->SetContent(
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(5)
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString("The window will close automatically once the current p4 operation is finished."))
				]
				+SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Cancel Operations")))
					.OnClicked_Lambda([this]() { ModelInterface->CancelP4Operations(); return FReply::Handled(); })
				]
			]
		);

		FSlateApplication::Get().AddModalWindow(WaitForP4Dialog.ToSharedRef(), nullptr, true);
		WaitForP4Dialog->ShowWindow();

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Dialog = WaitForP4Dialog, Model = ModelInterface, Tab = MainTab](float InDeltaTime)
			{
				if (Model->IsP4OperationRunning())
				{
					return true;
				}

				Dialog->RequestDestroyWindow();
				Tab->RequestCloseTab();

				return false;
			})
		);

		return false;
	}

	return true;
}

