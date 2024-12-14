// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaRCExtension.h"
#include "AvaRCSignatureCustomization.h"
#include "Editor.h"
#include "Framework/Docking/LayoutExtender.h"
#include "IAvaSceneInterface.h"
#include "IRemoteControlUIModule.h"
#include "LevelEditor.h"
#include "RemoteControlPreset.h"
#include "RemoteControlTrackerComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/RemoteControlComponentsSubsystem.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "AvaRCExtension"

URemoteControlPreset* FAvaRCExtension::GetRemoteControlPreset() const
{
	const IAvaSceneInterface* const Scene = GetSceneObject<IAvaSceneInterface>();
	return Scene ? Scene->GetRemoteControlPreset() : nullptr;
}

void FAvaRCExtension::Activate()
{
	RegisterSignatureCustomization();
	OpenRemoteControlTab();
}

void FAvaRCExtension::Deactivate()
{
	CloseRemoteControlTab();
	UnregisterSignatureCustomization();
}

void FAvaRCExtension::ExtendLevelEditorLayout(FLayoutExtender& InExtender) const
{
	const FName RemoteControlTabId(TEXT("RemoteControl_RemoteControlPanel"));

	InExtender.ExtendLayout(LevelEditorTabIds::Sequencer
		, ELayoutExtensionPosition::Before
		, FTabManager::FTab(RemoteControlTabId, ETabState::ClosedTab));
}

void FAvaRCExtension::ExtendToolbarMenu(UToolMenu& InMenu)
{
	FToolMenuSection& Section = InMenu.FindOrAddSection(DefaultSectionName);

	FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("RemoteControlButton")
		,  FExecuteAction::CreateSP(this, &FAvaRCExtension::OpenRemoteControlTab)
		, LOCTEXT("RemoteControlLabel"  , "Remote Control")
		, LOCTEXT("RemoteControlTooltip", "Opens the Remote Control Editor for the given Scene")
		, FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details")));

	Entry.StyleNameOverride = "CalloutToolbar";
}

void FAvaRCExtension::OnSceneObjectChanged(UObject* InOldSceneObject, UObject* InNewSceneObject)
{
	TSharedPtr<IAvaEditor> Editor = GetEditor();
	if (!Editor.IsValid())
	{
		return;
	}

	TSharedPtr<IToolkitHost> ToolkitHost = Editor->GetToolkitHost();
	if (!ToolkitHost.IsValid())
	{
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	check(AssetEditorSubsystem);

	if (const IAvaSceneInterface* OldScene = Cast<IAvaSceneInterface>(InOldSceneObject))
	{
		if (URemoteControlPreset* OldPreset = OldScene->GetRemoteControlPreset())
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(OldPreset);
		}
	}

	if (const IAvaSceneInterface* NewScene = Cast<IAvaSceneInterface>(InNewSceneObject))
	{
		if (URemoteControlPreset* NewPreset = NewScene->GetRemoteControlPreset())
		{
			// Level-dependent bindings are resolved via the Selected World in the Preset. By default, this should be the current edited world.
			// However, the Current Edited World does not hold ownership of the other sublevels, and so doing a "FindObject" or similar with this world
			// will fail for any actor/subobject within these sublevels.
			// So set the Selected World to be the true outer world of the new scene object level.
			NewPreset->SelectedWorld = InNewSceneObject->GetTypedOuter<UWorld>();

			AssetEditorSubsystem->OpenEditorForAsset(NewPreset, EToolkitMode::WorldCentric, ToolkitHost);
		}
	}
}

void FAvaRCExtension::OpenRemoteControlTab() const
{
	TSharedPtr<IAvaEditor> Editor = GetEditor();
	if (!Editor.IsValid())
	{
		return;
	}

	TSharedPtr<IToolkitHost> ToolkitHost = Editor->GetToolkitHost();
	if (!ToolkitHost.IsValid())
	{
		return;
	}

	if (URemoteControlPreset* const RemoteControlPreset = GetRemoteControlPreset())
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		check(AssetEditorSubsystem);
		AssetEditorSubsystem->OpenEditorForAsset(RemoteControlPreset, EToolkitMode::WorldCentric, ToolkitHost);
	}
}

void FAvaRCExtension::CloseRemoteControlTab() const
{
	if (URemoteControlPreset* const RemoteControlPreset = GetRemoteControlPreset())
	{
		if (UAssetEditorSubsystem* const AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(RemoteControlPreset);
		}
	}
}

void FAvaRCExtension::RegisterSignatureCustomization()
{
	UnregisterSignatureCustomization();

	SignatureCustomization = MakeShared<FAvaRCSignatureCustomization>();
	IRemoteControlUIModule::Get().RegisterSignatureCustomization(SignatureCustomization);
}

void FAvaRCExtension::UnregisterSignatureCustomization()
{
	if (!SignatureCustomization.IsValid())
	{
		return;
	}

	if (IRemoteControlUIModule* RCUIModule = FModuleManager::GetModulePtr<IRemoteControlUIModule>(TEXT("RemoteControlUI")))
	{
		RCUIModule->UnregisterSignatureCustomization(SignatureCustomization);
	}

	SignatureCustomization.Reset();
}

#undef LOCTEXT_NAMESPACE
