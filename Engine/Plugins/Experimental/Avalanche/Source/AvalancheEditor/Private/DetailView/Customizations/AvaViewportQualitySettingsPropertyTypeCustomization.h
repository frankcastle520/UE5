// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Layout/SWrapBox.h"

class FName;
class FReply;
class FStructOnScope;
struct FAvaViewportQualitySettings;
struct FPropertyChangedEvent;

class FAvaViewportQualitySettingsPropertyTypeCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& DetailBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	//~ End IPropertyTypeCustomization

protected:
	void RefreshPresets();

	FAvaViewportQualitySettings& GetStructRef() const;

	FReply HandleDefaultsButtonClick();
	FReply HandleEnableAllButtonClick();
	FReply HandleDisableAllButtonClick();
	FReply HandlePresetButtonClick(const FName InPresetName);

	bool IsDefaultsButtonEnabled() const;
	bool IsAllButtonEnabled() const;
	bool IsNoneButtonEnabled() const;
	bool IsPresetButtonEnabled(const FName InPresetName) const;

	TSharedPtr<IPropertyHandle> StructPropertyHandle;

	TSharedPtr<SWrapBox> PresetsWrapBox;
};
