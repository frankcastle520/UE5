// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubSettingsCustomization.h"

#include "Config/LiveLinkHubTemplateTokens.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "LiveLinkHubSettings.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LiveLinkHubSettingsCustomization"

TSharedRef<IDetailCustomization> FLiveLinkHubSettingsCustomization::MakeInstance()
{
	return MakeShared<FLiveLinkHubSettingsCustomization>();
}

void FLiveLinkHubSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Update current value when opening the settings page. Not safe to do under the settings object since there isn't
	// an explicit callback when the settings page is opened, and using a method like PostInitProperties fires on the CDO
	// early in the startup process.
	GetMutableDefault<ULiveLinkHubSettings>()->CalculateExampleOutput();
	
	const TSharedRef<IPropertyHandle> AutomaticTokensHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkHubSettings, AutomaticTokens));
	IDetailPropertyRow* AutomaticTokensRow = DetailBuilder.EditDefaultProperty(AutomaticTokensHandle);
	check(AutomaticTokensRow);
	
	const FLiveLinkHubAutomaticTokens& AutomaticTokens = FLiveLinkHubAutomaticTokens::GetStaticTokens();
	FString FormattedTokensString;
	
	for (TFieldIterator<FProperty> It(FLiveLinkHubAutomaticTokens::StaticStruct()); It; ++It)
	{
		FProperty* Property = *It;
		if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			const FString PropertyValue = StrProperty->GetPropertyValue_InContainer(&AutomaticTokens);
			const FString Token = UE::LiveLinkHub::Tokens::Private::CreateToken(PropertyValue);

			const FString Tooltip = Property->GetToolTipText().ToString();
			if (!Tooltip.IsEmpty())
			{
				FormattedTokensString += FString::Printf(TEXT("%s - %s\n"), *Token, *Tooltip);
			}
		}
	}

	AutomaticTokensRow->CustomWidget()
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.f, 4.f)
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(AutomaticTokensHandle->GetPropertyDisplayName())
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FormattedTokensString))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];
}

#undef LOCTEXT_NAMESPACE