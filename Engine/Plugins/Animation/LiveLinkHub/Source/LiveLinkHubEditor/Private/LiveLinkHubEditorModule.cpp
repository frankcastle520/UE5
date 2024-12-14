// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubEditorModule.h"

#include "DesktopPlatformModule.h"
#include "HAL/FileManager.h"
#include "ILauncherPlatform.h"
#include "LauncherPlatformModule.h"
#include "LiveLinkHubLauncherUtils.h"
#include "LiveLinkHubEditorSettings.h"
#include "Misc/App.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/MessageDialog.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SLiveLinkHubEditorStatusBar.h"
#include "ToolMenus.h"


static TAutoConsoleVariable<int32> CVarLiveLinkHubEnableStatusBar(
	TEXT("LiveLinkHub.EnableStatusBar"), 1,
	TEXT("Whether to enable showing the livelink hub status bar in the editor. Must be set before launching the editor."),
	ECVF_RenderThreadSafe);

DECLARE_LOG_CATEGORY_CLASS(LogLiveLinkHubEditor, Log, Log)

#define LOCTEXT_NAMESPACE "LiveLinkHubEditor"

void FLiveLinkHubEditorModule::StartupModule()
{
	if (!IsRunningCommandlet() && CVarLiveLinkHubEnableStatusBar.GetValueOnAnyThread())
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FLiveLinkHubEditorModule::OnPostEngineInit);
	}
}

void FLiveLinkHubEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	if (!IsRunningCommandlet() && CVarLiveLinkHubEnableStatusBar.GetValueOnAnyThread()) 
	{
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);
		UnregisterLiveLinkHubStatusBar();
	}
}

void FLiveLinkHubEditorModule::OnPostEngineInit()
{
	if (GEditor)
	{
		RegisterLiveLinkHubStatusBar();
		
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->AddSection("VirtualProductionSection", LOCTEXT("VirtualProductionSection", "Virtual Production"));

		Section.AddMenuEntry("LiveLinkHub",
			LOCTEXT("LiveLinkHubLabel", "Live Link Hub"),
			LOCTEXT("LiveLinkHubTooltip", "Launch the Live Link Hub app."),
			FSlateIcon("LiveLinkStyle", "LiveLinkClient.Common.Icon.Small"),
			FUIAction(FExecuteAction::CreateRaw(this, &FLiveLinkHubEditorModule::OpenLiveLinkHub)));
	}
}

void FLiveLinkHubEditorModule::OpenLiveLinkHub()
{
	FAsyncTaskNotificationConfig NotificationConfig;
	NotificationConfig.bKeepOpenOnFailure = true;
	NotificationConfig.TitleText = LOCTEXT("LaunchingLiveLinkHub", "Launching Live Link Hub...");
	NotificationConfig.LogCategory = &LogLiveLinkHubEditor;

	FAsyncTaskNotification Notification(NotificationConfig);
	const FText LaunchLiveLinkHubErrorTitle = LOCTEXT("LaunchLiveLinkHubErrorTitle", "Failed to Launch LiveLinkhub.");

	// Try getting the livelinkhub app location by reading a registry key.
	if (GetDefault<ULiveLinkHubEditorSettings>()->bDetectLiveLinkHubExecutable)
	{
		ILauncherPlatform* LauncherPlatform = FLauncherPlatformModule::Get();

		UE::LiveLinkHubLauncherUtils::FInstalledApp LiveLinkHubApp;
		if (UE::LiveLinkHubLauncherUtils::FindLiveLinkHubInstallation(LiveLinkHubApp))
		{
			// Found a LiveLinkHub installation from the launcher, so launch it that way.
			
			const FString LaunchLink = TEXT("apps") / LiveLinkHubApp.NamespaceId + TEXT("%3A") + LiveLinkHubApp.ItemId + TEXT("%3A") + LiveLinkHubApp.AppName + TEXT("?action=launch&silent=true");
			FOpenLauncherOptions OpenOptions(LaunchLink);
			if (!LauncherPlatform->OpenLauncher(OpenOptions))
			{
				Notification.SetComplete(
					LaunchLiveLinkHubErrorTitle,
					LOCTEXT("LaunchLiveLinkHubError_CouldNotOpenLauncher", "Could not launch Live Link Hub through the Epic Games Store."),
					false
				);

			}
			else
			{
				Notification.SetComplete(
					LOCTEXT("LiveLinkHubLaunchSuccessTitle", "Launched Live Link Hub."),
					LOCTEXT("LaunchLiveLinkHubError_LaunchSuccess", "Launching Liv Link Hub through the Epic Games Store."),
					true
				);
			}
		}
		else
		{
			const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("InstallThroughEGS", "Live Link Hub is not currently installed, do you want to install it through the Epic Games Store?"));

			if (Choice == EAppReturnType::Yes)
			{
				// Could not find LiveLinkHub from the launcher. Prompt the user to open the EGS and install it.
				if (!GetDefault<ULiveLinkHubEditorSettings>()->LiveLinkHubStorePage.IsEmpty())
				{
					FOpenLauncherOptions OpenOptions(GetDefault<ULiveLinkHubEditorSettings>()->LiveLinkHubStorePage);
					if (!LauncherPlatform->OpenLauncher(OpenOptions))
					{
						Notification.SetComplete(
							LaunchLiveLinkHubErrorTitle,
							LOCTEXT("LaunchLiveLinkHubError_CouldNotFindHubStorePage", "Could not find the Live Link Hub page on the Epic Games Store."),
							false
						);
					}
					else
					{
						Notification.SetComplete(
							LaunchLiveLinkHubErrorTitle,
							LOCTEXT("LaunchLiveLinkHub_LaunchFromStore", "Opening Epic Games Store to the Live Link Hub page."),
							true
						);
					}

				}
				else
				{
					Notification.SetComplete(
						LaunchLiveLinkHubErrorTitle,
						LOCTEXT("LaunchLiveLinkHubError_EmptyConfig", "Could not find the Live Link Hub page on the Epic Games Store, missing configuration for the store page."),
						false
					);
				
				}
			}
			else
			{
				Notification.SetComplete(
					LaunchLiveLinkHubErrorTitle,
					LOCTEXT("LaunchLiveLinkHub_DidNotLaunchFromStore", "Live Link Hub could not be launched since it wasn't installed."),
					false
				);
			}

		}

		return;
	}

	// Find livelink hub executable location for our build configuration
	FString LiveLinkHubPath = FPlatformProcess::GenerateApplicationPath(TEXT("LiveLinkHub"), FApp::GetBuildConfiguration());

	// Validate it exists and fall back to development if it doesn't.
	if (!IFileManager::Get().FileExists(*LiveLinkHubPath))
	{
		LiveLinkHubPath = FPlatformProcess::GenerateApplicationPath(TEXT("LiveLinkHub"), EBuildConfiguration::Development);

		// If it still doesn't exist, fall back to the shipping executable.
		if (!IFileManager::Get().FileExists(*LiveLinkHubPath))
		{
			LiveLinkHubPath = FPlatformProcess::GenerateApplicationPath(TEXT("LiveLinkHub"), EBuildConfiguration::Shipping);
		}
	}

	if (!IFileManager::Get().FileExists(*LiveLinkHubPath))
	{
		Notification.SetComplete(
			LaunchLiveLinkHubErrorTitle,
			LOCTEXT("LaunchLiveLinkHubError_ExecutableMissing", "Could not find the executable. Have you compiled the Live Link Hub app?"),
			false
		);

		return;
	}

	// Validate we do not have it running locally
	const FString AppName = FPaths::GetCleanFilename(LiveLinkHubPath);
	if (FPlatformProcess::IsApplicationRunning(*AppName))
	{
		Notification.SetComplete(
			LaunchLiveLinkHubErrorTitle,
			LOCTEXT("LaunchLiveLinkHubError_AlreadyRunning", "A Live Link Hub instance is already running."),
			false
		);
		return;
	}

	constexpr bool bLaunchDetached = true;
	constexpr bool bLaunchHidden = false;
	constexpr bool bLaunchReallyHidden = false;

	const FProcHandle ProcHandle = FPlatformProcess::CreateProc(*LiveLinkHubPath, TEXT(""), bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, nullptr, 0, nullptr, nullptr, nullptr);
	if (ProcHandle.IsValid())
	{
		Notification.SetComplete(
			LOCTEXT("LaunchedLiveLinkHub", "Launched Live Link Hub"), FText(), true);

		return;
	}
	else // Very unlikely in practice, but possible in theory.
	{
		Notification.SetComplete(
			LaunchLiveLinkHubErrorTitle,
			LOCTEXT("LaunchLiveLinkHubError_InvalidHandle", "Failed to create the Live Link Hub process."),
			false);
	}
}

void FLiveLinkHubEditorModule::RegisterLiveLinkHubStatusBar()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.StatusBar.ToolBar"));

	FToolMenuSection& LiveLinkHubSection = Menu->AddSection(TEXT("LiveLinkHub"), FText::GetEmpty(), FToolMenuInsert(NAME_None, EToolMenuInsertType::First));

	LiveLinkHubSection.AddEntry(
		FToolMenuEntry::InitWidget(TEXT("LiveLinkHubStatusBar"), CreateLiveLinkHubWidget(), FText::GetEmpty(), true, false)
	);
}

void FLiveLinkHubEditorModule::UnregisterLiveLinkHubStatusBar()
{
	UToolMenus::UnregisterOwner(this);
}

TSharedRef<SWidget> FLiveLinkHubEditorModule::CreateLiveLinkHubWidget()
{
	return SNew(SLiveLinkHubEditorStatusBar);
}

IMPLEMENT_MODULE(FLiveLinkHubEditorModule, LiveLinkHubEditor);

#undef LOCTEXT_NAMESPACE /*LiveLinkHubEditor*/ 
