// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SCommonEditorViewportToolbarBase.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

class FMenuBuilder;
class ICommonEditorViewportToolbarInfoProvider;
class SEditorViewportViewMenu;

/**
 * Viewport toolbar widget used by the Chaos Visual Debugger
 */
class CHAOSVD_API SChaosVDViewportToolbar : public SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SChaosVDViewportToolbar)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<ICommonEditorViewportToolbarInfoProvider> InInfoProvider);

	static inline FName ShowMenuName = FName("ChaosVDViewportToolbarBase.Show");

protected:
	virtual TSharedRef<SEditorViewportViewMenu> MakeViewMenu() override;
	virtual void ExtendOptionsMenu(FMenuBuilder& OptionsMenuBuilder) const override;

	TSharedRef<SWidget> GenerateGoToLocationWidget() const;
	TSharedRef<SWidget> GenerateFrameRateOverrideValueWidget() const;
	TSharedRef<SWidget> GenerateTrackingDistanceValueWidget() const;

	int32 OnGetFrameRateOverrideValue() const;
	void OnFrameRateOverrideValueChanged(int32 NewFrameRate) const;
	bool IsUsingFrameRateOverride() const;

	float OnGetTrackingDistanceValue() const;
	void OnTrackingDistanceValueChanged(float NewTrackingDistance) const;
	bool IsAutoTrackingEnabled() const;

	void PopulateFrameRateSubmenu(FMenuBuilder& MenuBuilder) const;
	void PopulateAutoTrackingSubMenu(FMenuBuilder& MenuBuilder) const;

private:
	virtual TSharedRef<SWidget> GenerateShowMenu() const override;

	void HandleGoToLocationCommited(const FText& InLocationAsText, ETextCommit::Type Type) const;

};