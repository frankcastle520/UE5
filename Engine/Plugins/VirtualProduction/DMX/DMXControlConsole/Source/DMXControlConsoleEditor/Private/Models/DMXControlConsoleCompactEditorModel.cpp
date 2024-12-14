// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleCompactEditorModel.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorModule.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Views/SDMXControlConsoleCompactEditorView.h"
#include "Widgets/Docking/SDockTab.h"


void UDMXControlConsoleCompactEditorModel::SetControlConsole(UDMXControlConsole* InControlConsole)
{
	using namespace UE::DMX::Private;

	if (InControlConsole != SoftControlConsole)
	{
		// Stop playing dmx for the previous asset, if the console changed
		StopPlayingDMX();
	}

	SoftControlConsole = InControlConsole;
	SaveConfig();

	const FDMXControlConsoleEditorModule& EditorModule = FModuleManager::GetModuleChecked<FDMXControlConsoleEditorModule>(TEXT("DMXControlConsoleEditor"));
	TSharedPtr<SDockTab> CompactEditorTab = EditorModule.GetCompactEditorTab();
	if (InControlConsole)
	{
		if (CompactEditorTab.IsValid())
		{
			FGlobalTabmanager::Get()->DrawAttention(CompactEditorTab.ToSharedRef());
		}
		else
		{
			CompactEditorTab = FGlobalTabmanager::Get()->TryInvokeTab(EditorModule.CompactEditorTabId);
		}
		CompactEditorTab->SetContent(SNew(SDMXControlConsoleCompactEditorView));
	}
	else if (CompactEditorTab.IsValid())
	{
		CompactEditorTab->SetContent(SNullWidget::NullWidget);
	}
}

void UDMXControlConsoleCompactEditorModel::RestoreFullEditor()
{
	if (UDMXControlConsole* ControlConsole = LoadControlConsoleSynchronous())
	{
		// Clear the console before opening the asset editor, so that asset type actions for the control console don't try to open the compact tab
		SoftControlConsole = nullptr;
		SaveConfig();

		FText ErrorMsg;
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (ControlConsole && AssetEditorSubsystem->CanOpenEditorForAsset(ControlConsole, EAssetTypeActivationOpenedMethod::Edit, &ErrorMsg))
		{
			AssetEditorSubsystem->OpenEditorForAsset(ControlConsole, EAssetTypeActivationOpenedMethod::Edit);
		}

		const FDMXControlConsoleEditorModule& EditorModule = FModuleManager::GetModuleChecked<FDMXControlConsoleEditorModule>(TEXT("DMXControlConsoleEditor"));
		if (const TSharedPtr<SDockTab> CompactEditorTab = EditorModule.GetCompactEditorTab())
		{
			CompactEditorTab->SetContent(SNullWidget::NullWidget);
		}
	}
}

void UDMXControlConsoleCompactEditorModel::StopPlayingDMX()
{
	if (UDMXControlConsole* ControlConsole = LoadControlConsoleSynchronous())
	{
		ControlConsole->GetControlConsoleData()->StopSendingDMX();
	}
}

bool UDMXControlConsoleCompactEditorModel::IsUsingControlConsole(const UDMXControlConsole* ControlConsole) const
{
	return SoftControlConsole == ControlConsole;
}

UDMXControlConsole* UDMXControlConsoleCompactEditorModel::LoadControlConsoleSynchronous() const
{
	return SoftControlConsole.LoadSynchronous();
}
