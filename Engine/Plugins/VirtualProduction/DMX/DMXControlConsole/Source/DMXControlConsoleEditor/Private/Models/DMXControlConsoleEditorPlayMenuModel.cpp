// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorPlayMenuModel.h"

#include "Commands/DMXControlConsoleEditorCommands.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXEditorStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorPlayMenuModel"

void UDMXControlConsoleEditorPlayMenuModel::Initialize(UDMXControlConsole* InControlConsole, const TSharedRef<FUICommandList>& InCommandList)
{
	CommandList = InCommandList;
	ControlConsole = InControlConsole;
	ControlConsoleData = ControlConsole ? ControlConsole->GetControlConsoleData() : nullptr;
	if (!ensureMsgf(CommandList.IsValid() || IsValid(ControlConsole) && IsValid(ControlConsoleData), TEXT("Cannot setup control console PlayMenuModel. Invalid data provided")))
	{
		return;
	}

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().PlayDMX,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::PlayDMX),
		FCanExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanPlayDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanPlayDMX)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().PauseDMX,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::PauseDMX),
		FCanExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanPauseDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanPauseDMX)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().ResumeDMX,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::PlayDMX),
		FCanExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanResumeDMX),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanResumeDMX)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().StopDMX,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::StopPlayingDMX),
		FCanExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::CanStopPlayingDMX)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().TogglePlayPauseDMX,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::TogglePlayPauseDMX)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().TogglePlayStopDMX,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::TogglePlayStopDMX)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().EditorStopKeepsLastValues,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::SetStopDMXMode, EDMXControlConsoleStopDMXMode::DoNotSendValues),
		FCanExecuteAction(),
		FIsActionChecked::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::IsUsingStopDMXMode, EDMXControlConsoleStopDMXMode::DoNotSendValues)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().EditorStopSendsDefaultValues,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::SetStopDMXMode, EDMXControlConsoleStopDMXMode::SendDefaultValues),
		FCanExecuteAction(),
		FIsActionChecked::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::IsUsingStopDMXMode, EDMXControlConsoleStopDMXMode::SendDefaultValues)
	);

	CommandList->MapAction(
		FDMXControlConsoleEditorCommands::Get().EditorStopSendsZeroValues,
		FExecuteAction::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::SetStopDMXMode, EDMXControlConsoleStopDMXMode::SendZeroValues),
		FCanExecuteAction(),
		FIsActionChecked::CreateUObject(this, &UDMXControlConsoleEditorPlayMenuModel::IsUsingStopDMXMode, EDMXControlConsoleStopDMXMode::SendZeroValues)
	);
}

void UDMXControlConsoleEditorPlayMenuModel::CreatePlayMenu(UToolMenu& InMenu)
{
	// Play section
	{
		FToolMenuSection& PlaySection = InMenu.AddSection("PlayMenu");

		// Play
		FToolMenuEntry PlayMenuEntry = FToolMenuEntry::InitToolBarButton(
			FDMXControlConsoleEditorCommands::Get().PlayDMX,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDMXEditorStyle::Get().GetStyleSetName(), "Icons.PlayDMX"));

		PlayMenuEntry.StyleNameOverride = FName("Toolbar.BackplateLeftPlay");
		PlaySection.AddEntry(PlayMenuEntry);

		// Pause
		FToolMenuEntry PauseMenuEntry = FToolMenuEntry::InitToolBarButton(
			FDMXControlConsoleEditorCommands::Get().PauseDMX,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDMXEditorStyle::Get().GetStyleSetName(), "Icons.PauseDMX"));

		PauseMenuEntry.StyleNameOverride = FName("Toolbar.BackplateLeft");
		PlaySection.AddEntry(PauseMenuEntry);

		// Resume
		FToolMenuEntry ResumeMenuEntry = FToolMenuEntry::InitToolBarButton(
			FDMXControlConsoleEditorCommands::Get().ResumeDMX,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDMXEditorStyle::Get().GetStyleSetName(), "Icons.ResumeDMX"));

		ResumeMenuEntry.StyleNameOverride = FName("Toolbar.BackplateLeftPlay");
		PlaySection.AddEntry(ResumeMenuEntry);

		// Stop
		FToolMenuEntry StopMenuEntry = FToolMenuEntry::InitToolBarButton(
			FDMXControlConsoleEditorCommands::Get().StopDMX,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FDMXEditorStyle::Get().GetStyleSetName(), "Icons.StopDMX"));

		StopMenuEntry.StyleNameOverride = FName("Toolbar.BackplateCenterStop");
		PlaySection.AddEntry(StopMenuEntry);

		// Playback Settings
		const TWeakObjectPtr<UDMXControlConsoleEditorPlayMenuModel> WeakMenuModel = this;
		FToolMenuEntry PlaybackSettingsComboEntry = FToolMenuEntry::InitComboButton(
			"PlaybackSettings",
			FToolUIActionChoice(),
			FOnGetContent::CreateLambda([WeakMenuModel]
				{
					const TSharedPtr<FUICommandList> ContextualCommandList = WeakMenuModel.IsValid() ? WeakMenuModel->GetCommandList() : nullptr;
					if (!ContextualCommandList.IsValid())
					{
						return SNullWidget::NullWidget;
					}

					constexpr bool bShouldCloseWindowAfterMenuSelection = true;
					FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, ContextualCommandList);

					MenuBuilder.BeginSection("ResetDMXModeSection");
					{
						MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().EditorStopSendsDefaultValues);
						MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().EditorStopSendsZeroValues);
						MenuBuilder.AddMenuEntry(FDMXControlConsoleEditorCommands::Get().EditorStopKeepsLastValues);
					}
					MenuBuilder.EndSection();

					return MenuBuilder.MakeWidget();
				}),
			LOCTEXT("PlaybackSettingsLabel", "DMX Playback Settings"),
			LOCTEXT("PlaybackSettingsToolTip", "Change DMX Playback Settings"));

		PlaybackSettingsComboEntry.StyleNameOverride = FName("Toolbar.BackplateRightCombo");
		PlaySection.AddEntry(PlaybackSettingsComboEntry);
	}
}

bool UDMXControlConsoleEditorPlayMenuModel::CanPlayDMX() const
{
	return !IsPlayingDMX() && !IsPausedDMX();
}

bool UDMXControlConsoleEditorPlayMenuModel::CanResumeDMX() const
{
	return IsPausedDMX() && !IsPlayingDMX();
}

void UDMXControlConsoleEditorPlayMenuModel::PlayDMX()
{
	if (ControlConsoleData)
	{
		ControlConsoleData->StartSendingDMX();
	}
}

bool UDMXControlConsoleEditorPlayMenuModel::CanPauseDMX() const
{
	return IsPlayingDMX();
}

void UDMXControlConsoleEditorPlayMenuModel::PauseDMX()
{
	if (ControlConsoleData)
	{
		ControlConsoleData->PauseSendingDMX();
	}
}

bool UDMXControlConsoleEditorPlayMenuModel::CanStopPlayingDMX() const
{
	return IsPlayingDMX() || IsPausedDMX();
}

void UDMXControlConsoleEditorPlayMenuModel::StopPlayingDMX()
{
	if (ControlConsoleData)
	{
		ControlConsoleData->StopSendingDMX();
	}
}

void UDMXControlConsoleEditorPlayMenuModel::TogglePlayPauseDMX()
{
	if (IsPlayingDMX())
	{
		PauseDMX();
	}
	else
	{
		PlayDMX();
	}
}

void UDMXControlConsoleEditorPlayMenuModel::TogglePlayStopDMX()
{
	if (IsPlayingDMX())
	{
		StopPlayingDMX();
	}
	else
	{
		PlayDMX();
	}
}

void UDMXControlConsoleEditorPlayMenuModel::SetStopDMXMode(EDMXControlConsoleStopDMXMode StopDMXMode)
{
	// Intentionally without transaction, changes should not follow undo/redo
	if (ControlConsoleData)
	{
		ControlConsoleData->MarkPackageDirty();
		ControlConsoleData->SetStopDMXMode(StopDMXMode);
	}
}

bool UDMXControlConsoleEditorPlayMenuModel::IsUsingStopDMXMode(EDMXControlConsoleStopDMXMode TestStopDMXMode) const
{
	return ControlConsoleData && ControlConsoleData->GetStopDMXMode() == TestStopDMXMode;
}

bool UDMXControlConsoleEditorPlayMenuModel::IsPlayingDMX() const
{
	return ControlConsoleData && ControlConsoleData->IsSendingDMX();
}

bool UDMXControlConsoleEditorPlayMenuModel::IsPausedDMX() const
{
	return ControlConsoleData && ControlConsoleData->IsPausedDMX();
}

#undef LOCTEXT_NAMESPACE
