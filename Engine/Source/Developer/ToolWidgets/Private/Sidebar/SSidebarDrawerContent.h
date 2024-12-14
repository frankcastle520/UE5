// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "Widgets/SCompoundWidget.h"

class FSidebarDrawer;
class ISidebarDrawerContent;
class SScrollBox;
class SWrapBox;

/** Handles drawer multi-section display */
class SSidebarDrawerContent : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSidebarDrawerContent) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<FSidebarDrawer>& InOwnerDrawerWeak);

	void BuildContent();

private:
	void OnSectionSelected(const ECheckBoxState InCheckBoxState, const FName InSectionName);

	bool IsSectionSelected(const FName InSectionName) const;

	bool ShouldShowSection(const TWeakPtr<ISidebarDrawerContent>& InSectionWeak) const;

	EVisibility GetSectionButtonVisibility(TWeakPtr<ISidebarDrawerContent> InSectionWeak) const;
	
	EVisibility GetSectionContentVisibility(const FName InSectionName, TWeakPtr<ISidebarDrawerContent> InSectionWeak) const;

	ECheckBoxState GetSectionCheckBoxState(const FName InSectionName) const;

	TArray<TSharedRef<ISidebarDrawerContent>> GetOrderedSections() const;

	void AddContentSlot(const TSharedRef<ISidebarDrawerContent>& InDrawerContent, const TOptional<float>& InFillSize = TOptional<float>());

	TWeakPtr<FSidebarDrawer> OwnerDrawerWeak;

	TSharedPtr<SWrapBox> ButtonBox;
	TSharedPtr<SScrollBox> ContentBox;
};
