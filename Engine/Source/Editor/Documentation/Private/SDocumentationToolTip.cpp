// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDocumentationToolTip.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SToolTip.h"
#include "Styling/AppStyle.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "IDocumentationPage.h"
#include "IDocumentation.h"
#include "DocumentationLink.h"
#include "ISourceCodeAccessor.h"
#include "ISourceCodeAccessModule.h"
#include "SourceControlHelpers.h"
#include "EngineAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "Widgets/Input/SHyperlink.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DocumentationStyleSet.h"
#include "SPrimaryButton.h"

void SDocumentationToolTip::Construct( const FArguments& InArgs )
{
	TextContent = InArgs._Text;
	StyleInfo = FAppStyle::GetWidgetStyle<FTextBlockStyle>(InArgs._Style);
	SubduedStyleInfo = FAppStyle::GetWidgetStyle<FTextBlockStyle>(InArgs._SubduedStyle);
	HyperlinkTextStyleInfo = FAppStyle::GetWidgetStyle<FTextBlockStyle>(InArgs._HyperlinkTextStyle);
	HyperlinkButtonStyleInfo = FAppStyle::GetWidgetStyle<FButtonStyle>(InArgs._HyperlinkButtonStyle);
	KeybindStyleInfo = FDocumentationStyleSet::Get().GetWidgetStyle<FTextBlockStyle>("ToolTip.KeybindText");
	ColorAndOpacity = InArgs._ColorAndOpacity;
	DocumentationLink = InArgs._DocumentationLink;
	bAddDocumentation = InArgs._AddDocumentation;
	DocumentationMargin = InArgs._DocumentationMargin;
	IsDisplayingDocumentationLink = false;
	Shortcut = InArgs._Shortcut;
	OverrideFullTooltipContent = InArgs._OverrideExtendedToolTipContent;

	ExcerptName = InArgs._ExcerptName;
	IsShowingFullTip = false;

	if( InArgs._Content.Widget != SNullWidget::NullWidget )
	{
		// Widget content argument takes precedence
		// overrides the text content.
		OverrideContent = InArgs._Content.Widget;
	}

	SAssignNew(DocumentationControlBox, SHorizontalBox);
	SAssignNew(FullTipContent, SBox);
	if (OverrideFullTooltipContent.IsValid())
	{
		FullTipContent->SetContent(OverrideFullTooltipContent.ToSharedRef());
		FullTipContent->SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SDocumentationToolTip::GetOverriddenFullToolTipVisibility));
	}

	ConstructSimpleTipContent();

	ChildSlot
	[
		SAssignNew(WidgetContent, SBox)
		.Padding(2.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SimpleTipContent.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				FullTipContent.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility(this, &SDocumentationToolTip::GetControlVisibility)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(0.0)
						.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.TopSeparator"))
						[
							SNew(SBox)
							.HeightOverride(1.0f)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(9.0)
						.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.Header"))
						[
							DocumentationControlBox.ToSharedRef()
						]
					]
				]
			]
		]
	];

	bIsInTransition = false;
	TransitionStartTime = 0;
	TransitionLength = 0.2f;
	LastDesiredSize = SimpleTipContent->GetDesiredSize();
	TransitionStartSize = SimpleTipContent->GetDesiredSize();
	bFullTipContentIsReady = OverrideFullTooltipContent.IsValid();
}

void SDocumentationToolTip::ConstructSimpleTipContent()
{
	// If there a UDN file that matches the DocumentationLink path, and that page has an excerpt whose name
	// matches ExcerptName, and that excerpt has a variable named ToolTipOverride, use the content of that
	// variable instead of the default TextContent.
	if (!DocumentationLink.IsEmpty() && !ExcerptName.IsEmpty())
	{
		TSharedRef<IDocumentation> Documentation = IDocumentation::Get();
		if (Documentation->PageExists(DocumentationLink))
		{
			DocumentationPage = Documentation->GetPage(DocumentationLink, NULL);

			FExcerpt Excerpt;
			if (DocumentationPage->HasExcerpt(ExcerptName))
			{
				if (DocumentationPage->GetExcerpt(ExcerptName, Excerpt))
				{
					if (FString* TooltipValue = Excerpt.Variables.Find(TEXT("ToolTipOverride")))
					{
						TextContent = FText::FromString(*TooltipValue);
					}
				}
			}
		}
	}

	TSharedPtr< SVerticalBox > VerticalBox;
	TSharedPtr< SHorizontalBox > TextBox;
	if ( !OverrideContent.IsValid() )
	{
		SAssignNew( SimpleTipContent, SBox )
		[
			SNew(SBorder)
			.BorderImage(this, &SDocumentationToolTip::GetSimpleTipBorderStyle)
			.Padding(9.f)
			[
				SAssignNew( VerticalBox, SVerticalBox )
				+SVerticalBox::Slot()
				.FillHeight( 1.0f )
				[
					SAssignNew(TextBox, SHorizontalBox)

					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
					SNew( STextBlock )
					.Text( TextContent )
					.TextStyle( &StyleInfo )
					.ColorAndOpacity( ColorAndOpacity )
					.WrapTextAt_Static( &SToolTip::GetToolTipWrapWidth )
					]
				]
			]
		];

		TextBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SBox)
			.Visibility(this, &SDocumentationToolTip::GetShortcutVisibility)
			.Padding(9.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.KeybindBorder"))
				.Padding(4.0f, 2.0f)
				[
					SNew(STextBlock)
					.TextStyle(&KeybindStyleInfo)
					.Text(Shortcut)
				]
			]
		];
	
	}
	else
	{
		SAssignNew( SimpleTipContent, SBox )
		[
			SNew(SBorder)
			.BorderImage(this, &SDocumentationToolTip::GetSimpleTipBorderStyle)
			.Padding(9.f)
			[
				SAssignNew( VerticalBox, SVerticalBox )
				+SVerticalBox::Slot()
				.FillHeight( 1.0f )
				[
					OverrideContent.ToSharedRef()
				]
			]
		];
	}

	if (bAddDocumentation)
	{
		AddDocumentation(VerticalBox);
	}
}

void SDocumentationToolTip::AddDocumentation(TSharedPtr< SVerticalBox > VerticalBox)
{
	if ( !DocumentationLink.IsEmpty() && !OverrideFullTooltipContent.IsValid())
	{
		if ( !DocumentationPage.IsValid() )
		{
			DocumentationPage = IDocumentation::Get()->GetPage( DocumentationLink, NULL );
		}

		if ( DocumentationPage->HasExcerpt( ExcerptName ))
		{
			FText MacShortcut = NSLOCTEXT("SToolTip", "MacRichTooltipShortcut", "Command + Option");
			FText WinShortcut = NSLOCTEXT("SToolTip", "WinRichTooltipShortcut", "Ctrl + Alt");

			FText KeyboardShortcut;
#if PLATFORM_MAC
			KeyboardShortcut = MacShortcut;
#else
			KeyboardShortcut = WinShortcut;
#endif

			VerticalBox->AddSlot()
			.AutoHeight()
			.HAlign( HAlign_Right )
			[
				SNew(SBox)
				.Visibility(this, &SDocumentationToolTip::GetPromptVisibility)
				.Padding(0, 9, 0, 0)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(0.0f, 0.0f, 5.0f, 0.0f)
					[
						SNew( STextBlock )
						.TextStyle( &SubduedStyleInfo )
						.Text( NSLOCTEXT( "SToolTip", "AdvancedToolTipMessage", "Learn more: hold" ) )
					]
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew( SBorder )
						.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.ToggleKeybindBorder"))
						.Padding(4.0f, 2.0f)
						[
							SNew(STextBlock)
							.TextStyle(&SubduedStyleInfo)
							.Text(FText::Format(NSLOCTEXT("SToolTip", "AdvancedToolTipKeybind", "{0}"), KeyboardShortcut))
							.Visibility(this, &SDocumentationToolTip::GetPromptVisibility)
						]
					]
				]
			];
		}

		SAssignNew(DocumentationControlBox, SHorizontalBox);
		IsDisplayingDocumentationLink = GetDefault<UEditorPerProjectUserSettings>()->bDisplayDocumentationLink;
		if (IsDisplayingDocumentationLink)
		{

			FString OptionalExcerptName;
			if (!ExcerptName.IsEmpty())
			{
				OptionalExcerptName = FString(TEXT(" [")) + ExcerptName + TEXT("]");
			}
			DocumentationControlBox->AddSlot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			.Padding(0, 0, 9, 0)
			[
				SNew(STextBlock)
					.Text(FText::FromString(DocumentationLink + OptionalExcerptName))
					.TextStyle(&SubduedStyleInfo)
			];

			if (!DocumentationPage->HasExcerpt(ExcerptName) && FSlateApplication::Get().SupportsSourceAccess())
			{
				FString DocPath = FDocumentationLink::ToSourcePath(DocumentationLink, FInternationalization::Get().GetCurrentCulture());
				if (!FPaths::FileExists(DocPath))
				{
					DocPath = FPaths::ConvertRelativePathToFull(DocPath);
				}

				DocumentationControlBox->AddSlot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				[
					SNew(SHyperlink)
						.Text(NSLOCTEXT("SToolTip", "EditDocumentationMessage_Create", "create"))
						.TextStyle(&HyperlinkTextStyleInfo)
						.UnderlineStyle(&HyperlinkButtonStyleInfo)
						.OnNavigate(this, &SDocumentationToolTip::CreateExcerpt, DocPath, ExcerptName)
				];
			}
		}
	}
}

void SDocumentationToolTip::CreateExcerpt( FString FileSource, FString InExcerptName )
{
	FText CheckoutFailReason;
	bool bNewFile = true;
	bool bCheckoutOrAddSucceeded = true;
	if (FPaths::FileExists(FileSource))
	{
		// Check out the existing file
		bNewFile = false;
		bCheckoutOrAddSucceeded = SourceControlHelpers::CheckoutOrMarkForAdd(FileSource, NSLOCTEXT("SToolTip", "DocumentationSCCActionDesc", "tool tip excerpt"), FOnPostCheckOut(), /*out*/ CheckoutFailReason);
	}

	FArchive* FileWriter = IFileManager::Get().CreateFileWriter( *FileSource, EFileWrite::FILEWRITE_Append | EFileWrite::FILEWRITE_AllowRead | EFileWrite::FILEWRITE_EvenIfReadOnly );

	if (bNewFile)
	{
		FString UdnHeader;
		UdnHeader += "Availability:NoPublish";
		UdnHeader += LINE_TERMINATOR;
		UdnHeader += "Title:";
		UdnHeader += LINE_TERMINATOR;
		UdnHeader += "Crumbs:";
		UdnHeader += LINE_TERMINATOR;
		UdnHeader += "Description:";
		UdnHeader += LINE_TERMINATOR;

		FileWriter->Serialize( TCHAR_TO_ANSI( *UdnHeader ), UdnHeader.Len() );
	}

	FString NewExcerpt;
	NewExcerpt += LINE_TERMINATOR;
	NewExcerpt += "[EXCERPT:";
	NewExcerpt += InExcerptName;
	NewExcerpt += "]";
	NewExcerpt += LINE_TERMINATOR;

	NewExcerpt += TextContent.Get().ToString();
	NewExcerpt += LINE_TERMINATOR;

	NewExcerpt += "[/EXCERPT:";
	NewExcerpt += InExcerptName;
	NewExcerpt += "]";
	NewExcerpt += LINE_TERMINATOR;

	if (!bNewFile)
	{
		FileWriter->Seek( FMath::Max( FileWriter->TotalSize(), (int64)0 ) );
	}

	FileWriter->Serialize( TCHAR_TO_ANSI( *NewExcerpt ), NewExcerpt.Len() );

	FileWriter->Close();
	delete FileWriter;

	if (bNewFile)
	{
		// Add the new file
		bCheckoutOrAddSucceeded = SourceControlHelpers::CheckoutOrMarkForAdd(FileSource, NSLOCTEXT("SToolTip", "DocumentationSCCActionDesc", "tool tip excerpt"), FOnPostCheckOut(), /*out*/ CheckoutFailReason);
	}

	ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
	SourceCodeAccessModule.GetAccessor().OpenFileAtLine(FileSource, 0);

	if (!bCheckoutOrAddSucceeded)
	{
		FNotificationInfo Info(CheckoutFailReason);
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	ReloadDocumentation();
}

void SDocumentationToolTip::ConstructFullTipContent()
{
	TArray< FExcerpt > Excerpts;
	DocumentationPage->GetExcerpts( Excerpts );

	if ( Excerpts.Num() > 0 )
	{
		int32 ExcerptIndex = 0;
		if ( !ExcerptName.IsEmpty() )
		{
			for (int Index = 0; Index < Excerpts.Num(); Index++)
			{
				if ( Excerpts[ Index ].Name == ExcerptName )
				{
					ExcerptIndex = Index;
					break;
				}
			}
		}

		if ( !Excerpts[ ExcerptIndex ].Content.IsValid() )
		{
			DocumentationPage->GetExcerptContent( Excerpts[ ExcerptIndex ] );
		}

		if ( Excerpts[ ExcerptIndex ].Content.IsValid() )
		{
			TSharedPtr< SVerticalBox > Box;
			TSharedPtr< SWidget > FullTipBox = SNew(SBox)
			.Visibility(this, &SDocumentationToolTip::GetFullTipVisibility)
			.Padding(DocumentationMargin)
			[
				SAssignNew(Box, SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				.MaxHeight(900.f)
				[
					SNew(SBorder)
					.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.TopSeparator"))
					[
						SNew(SScrollBox)
						.Style(FDocumentationStyleSet::Get(), "ToolTip.ScrollBox")
						+SScrollBox::Slot()
						.Padding(9.0f)
						[
							Excerpts[ExcerptIndex].Content.ToSharedRef()
						]
					]
				]
			];

			FString* FullDocumentationLink = Excerpts[ ExcerptIndex ].Variables.Find( TEXT("ToolTipFullLink") );
			FString* ExcerptBaseUrl = Excerpts[ExcerptIndex].Variables.Find(TEXT("BaseUrl"));
			if ( FullDocumentationLink != NULL && !FullDocumentationLink->IsEmpty() )
			{
				FString BaseUrl = FString();
				if (ExcerptBaseUrl != NULL)
				{
					BaseUrl = *ExcerptBaseUrl;
				}

				Box->AddSlot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.TopSeparator"))
					.Padding(0.0f)
					[
						SNew(SBox)
						.HeightOverride(1.0f)
					]
				];

				Box->AddSlot()
				.AutoHeight()
				[
					SNew(SBorder)
					.Padding(9.0f)
					.BorderImage(FDocumentationStyleSet::Get().GetBrush("ToolTip.Header"))
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						[
							SNew(SPrimaryButton)
							.Icon(FAppStyle::Get().GetBrush("Icons.Help"))
							.Text(NSLOCTEXT("SToolTip", "LearnMoreButton", "Learn More Online"))
							.OnClicked_Static([](FString Link, FString BaseUrl) -> FReply {
								if (!IDocumentation::Get()->Open(Link, FDocumentationSourceInfo(TEXT("rich_tooltips")), BaseUrl))
								{
									FNotificationInfo Info(NSLOCTEXT("SToolTip", "FailedToOpenLink", "Failed to Open Link"));
									FSlateNotificationManager::Get().AddNotification(Info);
								}
								return FReply::Handled();
								}, *FullDocumentationLink, BaseUrl)
						]
					]
				];
			}

			if (IsDisplayingDocumentationLink && FSlateApplication::Get().SupportsSourceAccess() )
			{
				DocumentationControlBox->AddSlot()
				.AutoWidth()
				.HAlign( HAlign_Right )
				[
					SNew( SHyperlink )
					.Text( NSLOCTEXT( "SToolTip", "EditDocumentationMessage_Edit", "edit" ) )
					.TextStyle( &HyperlinkTextStyleInfo )
					.UnderlineStyle( &HyperlinkButtonStyleInfo )
					// todo: needs to update to point to the "real" source file used for the excerpt
					.OnNavigate_Static([](FString Link, int32 LineNumber) {
							ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>("SourceCodeAccess");
							SourceCodeAccessModule.GetAccessor().OpenFileAtLine(Link, LineNumber);
						}, FPaths::ConvertRelativePathToFull(FDocumentationLink::ToSourcePath(DocumentationLink, FInternationalization::Get().GetCurrentCulture())), Excerpts[ExcerptIndex].LineNumber)
				];
			}
			
			FullTipContent->SetContent(FullTipBox.ToSharedRef());
			bFullTipContentIsReady = true;
		}
	}
}

FReply SDocumentationToolTip::ReloadDocumentation()
{
	SimpleTipContent.Reset();
	DocumentationControlBox.Reset();
	bFullTipContentIsReady = false;

	ConstructSimpleTipContent();

	if ( DocumentationPage.IsValid() )
	{
		DocumentationPage->Reload();

		if ( DocumentationPage->HasExcerpt( ExcerptName ) )
		{
			ConstructFullTipContent();
		}
	}

	return FReply::Handled();
}

void SDocumentationToolTip::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	LastDesiredSize = WidgetContent->GetDesiredSize();

	const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
	const bool NeedsUpdate = !OverrideFullTooltipContent.IsValid() && IsDisplayingDocumentationLink != GetDefault<UEditorPerProjectUserSettings>()->bDisplayDocumentationLink;
	if (TransitionStartTime > 0)
	{
		TransitionPercentage = (InCurrentTime - TransitionStartTime) / TransitionLength;
		if (TransitionPercentage > 1.0f)
		{
			TransitionPercentage = 1.0f;
			// Stop transition.
			TransitionStartTime = 0;
		}

		const FVector2D TransitionEndSize = WidgetContent->GetDesiredSize();
		LastDesiredSize = TransitionStartSize
						- (((TransitionStartSize - TransitionEndSize))
						   * FMath::InterpEaseOut<float>(0.f, 1.f, TransitionPercentage, 4.f));
	}

	if ( !IsShowingFullTip && ModifierKeys.IsAltDown() && ModifierKeys.IsControlDown() )
	{
		if (!OverrideFullTooltipContent.IsValid())
		{
			if ( !bFullTipContentIsReady && DocumentationPage.IsValid() && DocumentationPage->HasExcerpt(ExcerptName))
			{
				ConstructFullTipContent();
			}
			else if ( GetDefault<UEditorPerProjectUserSettings>()->bDisplayDocumentationLink )
			{
				ReloadDocumentation();
			}
		}

		if ( bFullTipContentIsReady)
		{
			if (!OverrideFullTooltipContent.IsValid())
			{
				// Analytics event
				if (FEngineAnalytics::IsAvailable())
				{
					TArray<FAnalyticsEventAttribute> Params;
					Params.Add(FAnalyticsEventAttribute(TEXT("Page"), DocumentationLink));
					Params.Add(FAnalyticsEventAttribute(TEXT("Excerpt"), ExcerptName));

					FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.Documentation.FullTooltipShown"), Params);
				}
			}
			bIsInTransition = true;
			IsShowingFullTip = true;
			TransitionStartTime = InCurrentTime;
			TransitionStartSize = LastDesiredSize;
		}
	}
	else if ( ( IsShowingFullTip || NeedsUpdate )  && ( !ModifierKeys.IsAltDown() || !ModifierKeys.IsControlDown() ) )
	{
		if ( NeedsUpdate )
		{
			ReloadDocumentation();
			IsDisplayingDocumentationLink = GetDefault<UEditorPerProjectUserSettings>()->bDisplayDocumentationLink;
		}

		bIsInTransition = true;
		IsShowingFullTip = false;
		TransitionStartTime = InCurrentTime;
		TransitionStartSize = LastDesiredSize;
	}
}

bool SDocumentationToolTip::IsInteractive() const
{
	const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
	return ((OverrideFullTooltipContent.IsValid() || DocumentationPage.IsValid()) && ModifierKeys.IsAltDown() && ModifierKeys.IsControlDown() );
}

FVector2D SDocumentationToolTip::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return LastDesiredSize;
}

EVisibility SDocumentationToolTip::GetOverriddenFullToolTipVisibility() const
{
	return IsShowingFullTip ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDocumentationToolTip::GetFullTipVisibility() const
{
	if (IsShowingFullTip)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

EVisibility SDocumentationToolTip::GetPromptVisibility() const
{
	if (IsShowingFullTip)
	{
		return EVisibility::Collapsed;
	}
	return EVisibility::Visible;
}

EVisibility SDocumentationToolTip::GetControlVisibility() const
{
	if (IsDisplayingDocumentationLink && (IsShowingFullTip || !DocumentationPage.IsValid() || !DocumentationPage->HasExcerpt(ExcerptName)))
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

EVisibility SDocumentationToolTip::GetShortcutVisibility() const
{
	if ((Shortcut.IsSet() || Shortcut.IsBound()) && !Shortcut.Get().IsEmpty())
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

const FSlateBrush* SDocumentationToolTip::GetSimpleTipBorderStyle() const
{
	if (IsShowingFullTip && !OverrideContent.IsValid())
	{
		return FDocumentationStyleSet::Get().GetBrush("ToolTip.Header");
	}
	return FAppStyle::GetBrush("");
}
