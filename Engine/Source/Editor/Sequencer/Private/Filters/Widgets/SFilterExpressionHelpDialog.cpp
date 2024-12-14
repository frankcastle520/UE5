// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFilterExpressionHelpDialog.h"
#include "Filters/SequencerTextFilterExpressionContext.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SFilterExpressionHelpDialog"

const FSlateColor SFilterExpressionHelpDialog::KeyColor = FStyleColors::AccentBlue;
const FSlateColor SFilterExpressionHelpDialog::ValueColor = FStyleColors::AccentOrange;

void SFilterExpressionHelpDialog::Construct(const FArguments& InArgs)
{
	HeaderText = InArgs._HeaderText;
	DocumentationLink = InArgs._DocumentationLink;
	TextFilterExpressionContexts = InArgs._TextFilterExpressionContexts;

	SWindow::Construct(SWindow::FArguments()
		.Title(InArgs._DialogTitle)
		.AutoCenter(EAutoCenter::PrimaryWorkArea)
		.SizingRule(ESizingRule::Autosized)
		.HasCloseButton(true)
		.SupportsMaximize(false)
		.SupportsMinimize(false));

	const TSharedRef<SVerticalBox> ContentWidget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f)
		[
			ConstructDialogHeader()
		]
		+ SVerticalBox::Slot()
		.Padding(5.f)
		[
			SNew(SBox)
			.MaxDesiredWidth(MaxDesiredWidth)
			.MaxDesiredHeight(MaxDesiredHeight)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
				[
					ConstructExpressionWidgetList()
				]
			]
		];

	SetContent(ContentWidget);
}

TSharedRef<SWidget> SFilterExpressionHelpDialog::ConstructDialogHeader()
{
	const TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0.f, 0.f, 20.f, 0.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
			.Text(HeaderText)
		];

	if (!DocumentationLink.IsEmpty())
	{
		HorizontalBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(12.f))
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FAppStyle::Get().GetBrush(TEXT("Icons.Documentation")))
			];

		HorizontalBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SHyperlink)
				.Text(LOCTEXT("DocumentationLink", "Documentation"))
				.ToolTipText(FText::Format(LOCTEXT("NavigateToDocumentation", "Open the online documentation ({0})"), FText::FromString(DocumentationLink)))
				.Style(FAppStyle::Get(), TEXT("HoverOnlyHyperlink"))
				.TextStyle(FAppStyle::Get(), TEXT("SmallText"))
				.OnNavigate(this, &SFilterExpressionHelpDialog::OpenDocumentationLink)
			];
	}

	return HorizontalBox;
}

TSharedRef<SWidget> SFilterExpressionHelpDialog::ConstructExpressionWidgetList()
{
	const TSharedRef<SScrollBox> Container = SNew(SScrollBox);

	bool bIsFirst = true;

	for (const TSharedRef<FSequencerTextFilterExpressionContext>& ExpressionContext : TextFilterExpressionContexts)
	{
		if (bIsFirst)
		{
			bIsFirst = false;
		}
		else
		{
			Container->AddSlot()
				.AutoSize()
				[
					SNew(SSeparator)
				];
		}

		Container->AddSlot()
			.AutoSize()
			.Padding(0.f, 0.f, 5.f, 0.f)
			[
				ConstructExpressionWidget(ExpressionContext)
			];
	}

	return Container;
}

TSharedRef<SWidget> SFilterExpressionHelpDialog::ConstructExpressionWidget(const TSharedPtr<FSequencerTextFilterExpressionContext>& InExpressionContext)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ConstructKeysWidget(InExpressionContext->GetKeys())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(10.f, 0.f, 1.f, 0.f)
			[
				ConstructValueWidget(InExpressionContext->GetValueType())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(15.f, 0.f, 5.f, 5.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.TextStyle(FAppStyle::Get(), TEXT("NormalText"))
			.Text(InExpressionContext->GetDescription())
		];
}

TSharedRef<SWidget> SFilterExpressionHelpDialog::ConstructKeysWidget(const TSet<FName>& InKeys)
{
	const TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

	for (const FName KeyName : InKeys)
	{
		const FString KeyNameString = KeyName.ToString();

		if (HorizontalBox->GetChildren()->Num() > 0)
		{
			HorizontalBox->AddSlot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("KeySeparator", " | "))
				];
		}

		HorizontalBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.ColorAndOpacity(KeyColor)
				.Text(FText::FromName(KeyName))
			];
	}

	return HorizontalBox;
}

TSharedRef<SWidget> SFilterExpressionHelpDialog::ConstructValueWidget(const ESequencerTextFilterValueType InValueType)
{
	static const FText LessThanText = LOCTEXT("CommaLT", "<");
	static const FText GreaterThanText = LOCTEXT("CommaGT", ">");

	const TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

	auto AddValueTypeSlot = [&HorizontalBox](const FText& InText, const TOptional<FSlateColor> InColor = TOptional<FSlateColor>())
	{
		const TSharedRef<STextBlock> TextWidget = SNew(STextBlock)
			.Text(InText);

		if (InColor.IsSet())
		{
			TextWidget->SetColorAndOpacity(InColor.GetValue());
		}

		HorizontalBox->AddSlot()
			.AutoWidth()
			[
				TextWidget
			];
	};

	switch (InValueType)
	{
	case ESequencerTextFilterValueType::String:
		AddValueTypeSlot(LessThanText);
		AddValueTypeSlot(LOCTEXT("StringValue", "String"), ValueColor);
		AddValueTypeSlot(GreaterThanText);
		break;
	case ESequencerTextFilterValueType::Boolean:
		AddValueTypeSlot(LessThanText);
		AddValueTypeSlot(LOCTEXT("TrueValue", "True"), ValueColor);
		AddValueTypeSlot(LOCTEXT("Slash", "/"));
		AddValueTypeSlot(LOCTEXT("FalseValue", "False"), ValueColor);
		AddValueTypeSlot(GreaterThanText);
		break;
	case ESequencerTextFilterValueType::Integer:
		AddValueTypeSlot(LessThanText);
		AddValueTypeSlot(LOCTEXT("IntegerValue", "###"), ValueColor);
		AddValueTypeSlot(GreaterThanText);
		break;
	}

	return HorizontalBox;
}

void SFilterExpressionHelpDialog::OpenDocumentationLink() const
{
	if (!DocumentationLink.IsEmpty())
	{
		FPlatformProcess::LaunchURL(*DocumentationLink, nullptr, nullptr);
	}
}

#undef LOCTEXT_NAMESPACE
