// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "IDetailCustomization.h"
#include "Widgets/Input/SComboBox.h"

class UInterchangeUsdTranslatorSettings;
class IDetailLayoutBuilder;

class FInterchangeUsdTranslatorSettingsCustomization : public IDetailCustomization
{
public:
	FInterchangeUsdTranslatorSettingsCustomization();
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	// End of IDetailCustomization interface

private:
	UInterchangeUsdTranslatorSettings* CurrentOptions;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> RenderContextComboBox;
	TArray<TSharedPtr<FString>> RenderContextComboBoxItems;

	TArray<TSharedPtr<FString>> MaterialPurposeComboBoxItems;
};

#endif	  // WITH_EDITOR
