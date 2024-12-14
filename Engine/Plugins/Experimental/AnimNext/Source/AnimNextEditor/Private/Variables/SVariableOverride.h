// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Widgets/Images/SImage.h"

namespace UE::AnimNext::Editor
{

// Widget used to display and manipulate variable overrides
class SVariableOverride : public SImage
{
	SLATE_BEGIN_ARGS(SVariableOverride) {}

	SLATE_ATTRIBUTE(EAnimNextDataInterfaceValueOverrideStatus, OverrideStatus)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAnimNextDataInterfaceEntry* InDataInterfaceEntry, FName InVariableName);

	void SetupMenu();

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	TSharedRef<SWidget> GetMenuContent();

	const FSlateBrush* GetBrush() const;

	FSlateColor GetColor() const;

	FText GetToolTipText() const;

	void OverrideVariable();

	bool CanOverrideVariable() const;

	void ClearOverride();

	bool CanClearOverride() const;

	void ResetToDefault();

	bool CanResetToDefault() const;

	bool IsResetToDefaultVisible() const;

	TAttribute<EAnimNextDataInterfaceValueOverrideStatus> OverrideStatus;
	TWeakObjectPtr<UAnimNextDataInterfaceEntry> WeakDataInterfaceEntry;
	FName VariableName;
};

}
