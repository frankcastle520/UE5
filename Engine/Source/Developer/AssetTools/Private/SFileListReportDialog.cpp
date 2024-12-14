// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFileListReportDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/ITableRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableViewBase.h"

#define LOCTEXT_NAMESPACE "FileListReportDialog"

void SFileListReportDialog::Construct(const FArguments& InArgs)
{
	for (const FText& File : InArgs._Files)
	{
		Files.Add(MakeShareable(new FText(File)));
	}

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Docking.Tab.ContentAreaBrush"))
				.Padding(FMargin(4, 8, 4, 4))
				[
					SNew(SVerticalBox)

						// Title text
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock).Text(InArgs._Header)
						]

						// Files To Sync list
						+ SVerticalBox::Slot()
						.Padding(0, 8)
						.FillHeight(1.f)
						[
							SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
								[
									SNew(SListView<TSharedRef<FText>>)
										.ListItemsSource(&Files)
										.SelectionMode(ESelectionMode::None)
										.OnGenerateRow(this, &SFileListReportDialog::MakeListViewWidget)
								]
						]

						// Close button
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 4)
						.HAlign(HAlign_Right)
						[
							SNew(SButton)
								.OnClicked(this, &SFileListReportDialog::CloseClicked)
								.Text(LOCTEXT("WindowCloseButton", "Close"))
						]
				]
		];
}

void SFileListReportDialog::OpenDialog(const FText& InTitle, const FText& InHeader, const TArray<FText>& InFiles, bool bOpenAsModal /*= false*/)
{
	TSharedRef<SWindow> FileListReportWindow = SNew(SWindow)
		.Title(InTitle)
		.ClientSize(FVector2D(800, 400))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SFileListReportDialog).Header(InHeader).Files(InFiles)
		];

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		if (bOpenAsModal)
		{
			FSlateApplication::Get().AddModalWindow(FileListReportWindow, MainFrameModule.GetParentWindow().ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindowAsNativeChild(FileListReportWindow, MainFrameModule.GetParentWindow().ToSharedRef());
		}
	}
	else
	{
		FSlateApplication::Get().AddWindow(FileListReportWindow);
	}
}

TSharedRef<ITableRow> SFileListReportDialog::MakeListViewWidget(TSharedRef<FText> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
		SNew(STableRow< TSharedRef<FText> >, OwnerTable)
		[
			SNew(STextBlock).Text(Item.Get())
		];
}

FReply SFileListReportDialog::CloseClicked()
{
	TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
