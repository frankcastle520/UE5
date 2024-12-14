// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConfirmDialogWidget.h"

#include "View/SubmitToolStyle.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SRichTextBlock.h"

void SConfirmDialogWidget::Construct(const FArguments& InArgs)
{
	ResultsCallback = InArgs._ResultCallback;
	
	TSharedPtr<SHorizontalBox> ButtonBox;
	
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(5)
		.VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5)
			[
				SNew(SRichTextBlock)
				.Text(InArgs._DescriptionText)
				.DecoratorStyleSet(&FAppStyle::Get())
				+SRichTextBlock::HyperlinkDecorator(TEXT("browser"), FSlateHyperlinkRun::FOnClick::CreateLambda(
					[](const FSlateHyperlinkRun::FMetadata& Metadata)
					{
						const FString* UrlPtr = Metadata.Find(TEXT("href"));
						if(UrlPtr)
						{
							FPlatformProcess::LaunchURL(**UrlPtr, nullptr, nullptr);
						}
					}))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
			.HAlign(HAlign_Right)
			[
				SAssignNew(ButtonBox, SHorizontalBox)
			]
		]
	];

	const FOnResult& ResultCallback = InArgs._ResultCallback;
	
	for(size_t i=0;i<InArgs._Buttons.Num();++i)
	{
		ConstructButton(ButtonBox.ToSharedRef(), i, InArgs._Buttons[i], i == 0);
	}
}

void SConfirmDialogWidget::ConstructButton(const TSharedRef<SHorizontalBox> Container, size_t Idx, const FString& ButtonText, bool IsPrimary)
{	
	Container->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(FText::FromString(ButtonText))
			.OnClicked_Lambda([this, Idx]()
			{
				ResultsCallback.ExecuteIfBound(Idx);
				return FReply::Handled();
			})
			.ButtonStyle(FSubmitToolStyle::Get(), IsPrimary ? "PrimaryButton" : "Button")
		];
}
