// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaViewportQualitySettingsPropertyTypeCustomization.h"
#include "AvaEditorSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Internationalization/Text.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Templates/SharedPointer.h"
#include "Viewport/AvaViewportQualitySettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "AvaViewportQualitySettingsPropertyTypeCustomization"

TSharedRef<IPropertyTypeCustomization> FAvaViewportQualitySettingsPropertyTypeCustomization::MakeInstance()
{
	return MakeShared<FAvaViewportQualitySettingsPropertyTypeCustomization>();
}

void FAvaViewportQualitySettingsPropertyTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructPropertyHandle = InStructPropertyHandle;

	if (InStructPropertyHandle->GetBoolMetaData(TEXT("HideHeader")))
	{
		HeaderRow.Visibility(EVisibility::Collapsed);
	}
	else
	{
		HeaderRow.NameContent()
			[
				InStructPropertyHandle->CreatePropertyNameWidget()
			];
	}
}

void FAvaViewportQualitySettingsPropertyTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle
	, IDetailChildrenBuilder& InOutDetailBuilder
	, IPropertyTypeCustomizationUtils& InOutStructCustomizationUtils)
{
	if (InStructPropertyHandle->GetBoolMetaData(TEXT("ShowPresets")))
	{
		InOutDetailBuilder.AddCustomRow(LOCTEXT("Presets", "Presets"))
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
				[
					SAssignNew(PresetsWrapBox, SWrapBox)
					.UseAllottedSize(true)
					.Orientation(EOrientation::Orient_Horizontal)
				]
			];

		RefreshPresets();
	}

	const TSharedPtr<IPropertyHandle> FeaturesProperty = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAvaViewportQualitySettings, Features));
	const TSharedRef<FDetailArrayBuilder> FeaturesArrayBuilder = MakeShared<FDetailArrayBuilder>(FeaturesProperty.ToSharedRef(), /*InGenerateHeader*/ false, /*InDisplayResetToDefault*/ true, /*InDisplayElementNum*/ false);

	FeaturesArrayBuilder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateLambda([](TSharedRef<IPropertyHandle> InElementPropertyHandle, const int32 InArrayIndex, IDetailChildrenBuilder& InOutChildrenBuilder)
		{
			TSharedPtr<IPropertyHandle> NameProperty = InElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAvaViewportQualitySettingsFeature, Name));
			TSharedPtr<IPropertyHandle> ValueProperty = InElementPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAvaViewportQualitySettingsFeature, bEnabled));

			FString FeatureName;
			NameProperty->GetValue(FeatureName);

			FText NameText, TooltipText;
			FAvaViewportQualitySettings::FeatureNameAndTooltipText(FeatureName, NameText, TooltipText);

			InOutChildrenBuilder.AddProperty(InElementPropertyHandle)
				.ToolTip(TooltipText)
				.CustomWidget()
				.NameContent()
				[
					SNew(STextBlock)
					.Text(NameText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				[
					ValueProperty->CreatePropertyValueWidget(/*bDisplayDefaultPropertyButtons=*/false)
				];
		}));

	InOutDetailBuilder.AddCustomBuilder(FeaturesArrayBuilder);
}

void FAvaViewportQualitySettingsPropertyTypeCustomization::RefreshPresets()
{
	if (!PresetsWrapBox.IsValid())
	{
		return;
	}

	PresetsWrapBox->ClearChildren();

	auto AddSlotToWrapBox = [this](const FName InName, FOnClicked InOnClicked = FOnClicked(), const TAttribute<bool>& InIsEnabled = true)
	{
		if (!InOnClicked.IsBound())
		{
			InOnClicked = FOnClicked::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::HandlePresetButtonClick, InName);
		}
		PresetsWrapBox->AddSlot()
			[
				SNew(SBox)
				.Padding(2.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(InOnClicked)
					.IsEnabled(InIsEnabled)
					[
						SNew(STextBlock)
						.TextStyle(FAppStyle::Get(), TEXT("SmallText"))
						.Text(FText::FromName(InName))
					]
				]
			];
	};

	AddSlotToWrapBox(TEXT("Defaults")
		, FOnClicked::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::HandleDefaultsButtonClick)
		, TAttribute<bool>::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::IsDefaultsButtonEnabled));

	AddSlotToWrapBox(TEXT("All")
		, FOnClicked::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::HandleEnableAllButtonClick)
		, TAttribute<bool>::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::IsAllButtonEnabled));

	AddSlotToWrapBox(TEXT("None")
		, FOnClicked::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::HandleDisableAllButtonClick)
		, TAttribute<bool>::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::IsNoneButtonEnabled));

	PresetsWrapBox->AddSlot()
		.Padding(5.f, 0.f)
		[
			SNew(SSeparator)
			.Orientation(EOrientation::Orient_Vertical)
		];

	for (const TPair<FName, FAvaViewportQualitySettings>& Preset : UAvaEditorSettings::Get()->ViewportQualityPresets)
	{
		AddSlotToWrapBox(Preset.Key
			, FOnClicked()
			, TAttribute<bool>::CreateSP(this, &FAvaViewportQualitySettingsPropertyTypeCustomization::IsPresetButtonEnabled, Preset.Key));
	}
}

FAvaViewportQualitySettings& FAvaViewportQualitySettingsPropertyTypeCustomization::GetStructRef() const
{
	check(StructPropertyHandle.IsValid());

	void* OutAddress = nullptr;
	StructPropertyHandle->GetValueData(OutAddress);

	return *reinterpret_cast<FAvaViewportQualitySettings*>(OutAddress);
}

FReply FAvaViewportQualitySettingsPropertyTypeCustomization::HandleDefaultsButtonClick()
{
	GetStructRef() = UAvaEditorSettings::Get()->DefaultViewportQualitySettings;

	return FReply::Handled();
}

FReply FAvaViewportQualitySettingsPropertyTypeCustomization::HandleEnableAllButtonClick()
{
	for (FAvaViewportQualitySettingsFeature& Feature : GetStructRef().Features)
	{
		Feature.bEnabled = true;
	}

	return FReply::Handled();
}

FReply FAvaViewportQualitySettingsPropertyTypeCustomization::HandleDisableAllButtonClick()
{
	for (FAvaViewportQualitySettingsFeature& Feature : GetStructRef().Features)
	{
		Feature.bEnabled = false;
	}

	return FReply::Handled();
}

FReply FAvaViewportQualitySettingsPropertyTypeCustomization::HandlePresetButtonClick(const FName InNewPresetName)
{
	UAvaEditorSettings* const EditorSettings = UAvaEditorSettings::Get();
	if (!EditorSettings->ViewportQualityPresets.Contains(InNewPresetName))
	{
		return FReply::Unhandled();
	}

	GetStructRef() = EditorSettings->ViewportQualityPresets[InNewPresetName];

	return FReply::Handled();
}

bool FAvaViewportQualitySettingsPropertyTypeCustomization::IsDefaultsButtonEnabled() const
{
	return UAvaEditorSettings::Get()->DefaultViewportQualitySettings != GetStructRef();
}

bool FAvaViewportQualitySettingsPropertyTypeCustomization::IsAllButtonEnabled() const
{
	for (FAvaViewportQualitySettingsFeature& Feature : GetStructRef().Features)
	{
		if (!Feature.bEnabled)
		{
			return true;
		}
	}

	return false;
}

bool FAvaViewportQualitySettingsPropertyTypeCustomization::IsNoneButtonEnabled() const
{
	for (FAvaViewportQualitySettingsFeature& Feature : GetStructRef().Features)
	{
		if (Feature.bEnabled)
		{
			return true;
		}
	}

	return false;
}

bool FAvaViewportQualitySettingsPropertyTypeCustomization::IsPresetButtonEnabled(const FName InPresetName) const
{
	const FAvaViewportQualitySettings& StructRef = GetStructRef();

	for (const TPair<FName, FAvaViewportQualitySettings>& Preset : UAvaEditorSettings::Get()->ViewportQualityPresets)
	{
		if (Preset.Key == InPresetName)
		{
			return Preset.Value != StructRef;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
