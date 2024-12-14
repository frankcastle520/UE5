// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVariableOverride.h"

#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "VariableOverrideCommands.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "SVariableOverride"

namespace UE::AnimNext::Editor
{

static FLazyName VariableOverrideMenuName("AnimNextVariableOverrides");

void SVariableOverride::Construct(const FArguments& InArgs, UAnimNextDataInterfaceEntry* InDataInterfaceEntry, FName InVariableName)
{
	WeakDataInterfaceEntry = InDataInterfaceEntry;
	VariableName = InVariableName;
	OverrideStatus = InArgs._OverrideStatus;

	SImage::Construct(
	SImage::FArguments()
		.ColorAndOpacity(this, &SVariableOverride::GetColor)
		.Image(this, &SVariableOverride::GetBrush)
	);

	SetToolTipText(MakeAttributeSP(this, &SVariableOverride::GetToolTipText));
}

void SVariableOverride::SetupMenu()
{
	const FVariableOverrideCommands& Commands = FVariableOverrideCommands::Get();
	TSharedRef<FUICommandList> ActionList = MakeShared<FUICommandList>();
	UToolMenu* ToolMenu = UToolMenus::Get()->RegisterMenu(VariableOverrideMenuName,NAME_None, EMultiBoxType::Menu, false);
	ToolMenu->bShouldCloseWindowAfterMenuSelection = true;
	ToolMenu->bCloseSelfOnly = true;

	ActionList->MapAction(
		Commands.OverrideVariable,
		FExecuteAction::CreateSP(this, &SVariableOverride::OverrideVariable),
		FCanExecuteAction::CreateSP(this, &SVariableOverride::CanOverrideVariable),
		EUIActionRepeatMode::RepeatDisabled);
	
	ActionList->MapAction(
		Commands.ClearOverride,
		FExecuteAction::CreateSP(this, &SVariableOverride::ClearOverride),
		FCanExecuteAction::CreateSP(this, &SVariableOverride::CanClearOverride),
		EUIActionRepeatMode::RepeatDisabled);

	ActionList->MapAction(
		Commands.ResetPropertyToDefault,
		FExecuteAction::CreateSP(this, &SVariableOverride::ResetToDefault),
		FCanExecuteAction::CreateSP(this, &SVariableOverride::CanResetToDefault),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &SVariableOverride::IsResetToDefaultVisible),
		EUIActionRepeatMode::RepeatDisabled);

	static const FName OverrideSectionName = TEXT("Overrides");
	FToolMenuSection& OverrideSection = ToolMenu->AddSection(OverrideSectionName);
	OverrideSection.AddMenuEntryWithCommandList(Commands.OverrideVariable, ActionList);
	OverrideSection.AddMenuEntryWithCommandList(Commands.ResetPropertyToDefault, ActionList);
	OverrideSection.AddMenuEntryWithCommandList(Commands.ClearOverride, ActionList);
}

FReply SVariableOverride::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	FSlateApplication::Get().PushMenu(
		SharedThis(this),
		FWidgetPath(),
		GetMenuContent(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup));

	return FReply::Handled();
}

const FSlateBrush* SVariableOverride::GetBrush() const
{
	const FSlateBrush* Image = nullptr;
	const FSlateBrush* HoveredImage = nullptr;

	switch(OverrideStatus.Get())
	{
	case EAnimNextDataInterfaceValueOverrideStatus::NotOverridden:
		Image = FAppStyle::GetBrush( "DetailsView.OverrideNone");
		HoveredImage = FAppStyle::GetBrush( "DetailsView.OverrideNone.Hovered");
		break;
	case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInThisAsset:
		Image = FAppStyle::GetBrush( "DetailsView.OverrideHere");
		HoveredImage = FAppStyle::GetBrush( "DetailsView.OverrideHere.Hovered");
		break;
	case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInParentAsset:
		Image = FAppStyle::GetBrush( "DetailsView.OverrideInherited");
		HoveredImage = FAppStyle::GetBrush( "DetailsView.OverrideInherited.Hovered");
		break;
	}

	return IsHovered() ? HoveredImage : Image;
}

FSlateColor SVariableOverride::GetColor() const
{
	return IsHovered() ? FStyleColors::White : FStyleColors::Foreground;
}

FText SVariableOverride::GetToolTipText() const
{
	switch(OverrideStatus.Get())
	{
	case EAnimNextDataInterfaceValueOverrideStatus::NotOverridden:
		return LOCTEXT("NotOverriddenTooltip", "This variable is not overriden");
	case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInThisAsset:
		return LOCTEXT("OverriddenInThisAssetTooltip", "This variable is overridden in this asset.");
	case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInParentAsset:
		return LOCTEXT("OverriddenInParentAssetTooltip", "This variable is overridden in a parent asset.");
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> SVariableOverride::GetMenuContent()
{
	SetupMenu();

	UToolMenu* ToolMenu = UToolMenus::Get()->FindMenu(VariableOverrideMenuName);
	check(ToolMenu);
	return UToolMenus::Get()->GenerateWidget(ToolMenu);
}

void SVariableOverride::OverrideVariable()
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("OverrideValue", "Override Value"));
	DataInterfaceEntry->SetValueOverrideToDefault(VariableName);
}

bool SVariableOverride::CanOverrideVariable() const
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return false;
	}

	return !DataInterfaceEntry->HasValueOverride(VariableName);
}

void SVariableOverride::ClearOverride()
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ClearOverrideValue", "Clear Override Value"));
	DataInterfaceEntry->ClearValueOverride(VariableName);
}

bool SVariableOverride::CanClearOverride() const
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return false;
	}

	return DataInterfaceEntry->HasValueOverride(VariableName);
}

void SVariableOverride::ResetToDefault()
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ResetToDefault", "Reset to default"));
	DataInterfaceEntry->ClearValueOverride(VariableName);
	DataInterfaceEntry->SetValueOverrideToDefault(VariableName);
}

bool SVariableOverride::CanResetToDefault() const
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return false;
	}

	return DataInterfaceEntry->HasValueOverride(VariableName);
}

bool SVariableOverride::IsResetToDefaultVisible() const
{
	UAnimNextDataInterfaceEntry* DataInterfaceEntry = WeakDataInterfaceEntry.Get();
	if(DataInterfaceEntry == nullptr)
	{
		return false;
	}

	return DataInterfaceEntry->HasValueOverrideNotMatchingDefault(VariableName);
}

}

#undef LOCTEXT_NAMESPACE