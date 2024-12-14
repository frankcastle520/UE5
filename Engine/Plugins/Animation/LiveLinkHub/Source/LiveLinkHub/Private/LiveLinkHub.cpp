// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHub.h"


#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Clients/LiveLinkHubClientsController.h"
#include "Clients/LiveLinkHubProvider.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "ISettingsModule.h"
#include "LiveLinkEditorSettings.h"
#include "LiveLinkHubClient.h"
#include "LiveLinkHubCommands.h"
#include "LiveLinkHubSubjectSettings.h"
#include "LiveLinkHubTicker.h"
#include "LiveLinkProviderImpl.h"
#include "LiveLinkSettings.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Recording/LiveLinkHubPlaybackController.h"
#include "Recording/LiveLinkHubRecordingController.h"
#include "Recording/LiveLinkHubRecordingListController.h"
#include "Session/LiveLinkHubSessionManager.h"
#include "Settings/LiveLinkHubSettings.h"
#include "Subjects/LiveLinkHubSubjectController.h"
#include "UI/Window/LiveLinkHubWindowController.h"

#define LOCTEXT_NAMESPACE "LiveLinkHub"


void FLiveLinkHub::Preinitialize(FLiveLinkHubTicker& Ticker)
{
	// We must register the livelink client first since we might rely on the modular feature to initialize the controllers/managers.
	if (GetDefault<ULiveLinkHubSettings>()->bTickOnGameThread)
	{
		LiveLinkHubClient = MakeShared<FLiveLinkHubClient>(AsShared());
	}
	else
	{
		LiveLinkHubClient = MakeShared<FLiveLinkHubClient>(AsShared(), Ticker.OnTick());
	}
	
	IModularFeatures::Get().RegisterModularFeature(ILiveLinkClient::ModularFeatureName, LiveLinkHubClient.Get());
}

void FLiveLinkHub::Initialize(bool bLauncherDistribution)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FLiveLinkHub::Initialize);

#if IS_PROGRAM
	// Re-enable this since we've disabled it to avoid the creation of the console window.
	GIsSilent = false;
#endif

	SessionManager = MakeShared<FLiveLinkHubSessionManager>();
	LiveLinkProvider = MakeShared<FLiveLinkHubProvider>(SessionManager.ToSharedRef());

	FModuleManager::Get().LoadModule("Settings");
	FModuleManager::Get().LoadModule("StatusBar");

	if (bLauncherDistribution)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		const FString FilePath = FPaths::Combine(FPlatformProcess::UserSettingsDir(), *FApp::GetEpicProductIdentifier(), TEXT("LiveLinkHub"), TEXT("Content"));
		AssetRegistry.ScanPathsSynchronous({ FilePath }, /*bForceRescan=*/ true);
	}

	CommandExecutor = MakeUnique<FConsoleCommandExecutor>();
	IModularFeatures::Get().RegisterModularFeature(IConsoleCommandExecutor::ModularFeatureName(), CommandExecutor.Get());

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FLiveLinkHub::InitializeControllers);
		RecordingController = MakeShared<FLiveLinkHubRecordingController>();
		PlaybackController = MakeShared<FLiveLinkHubPlaybackController>();
		RecordingListController = MakeShared<FLiveLinkHubRecordingListController>(AsShared());
		ClientsController = MakeShared<FLiveLinkHubClientsController>(LiveLinkProvider.ToSharedRef());
		CommandList = MakeShared<FUICommandList>();
		SubjectController = MakeShared<FLiveLinkHubSubjectController>();
	}


	FLiveLinkHubCommands::Register();
	BindCommands();
	
	FString LiveLinkHubLayoutIni = GConfig->GetConfigFilename(TEXT("LiveLinkHubLayout"));
	WindowController = MakeShared<FLiveLinkHubWindowController>(FLiveLinkHubWindowInitParams{ LiveLinkHubLayoutIni });
	WindowController->RestoreLayout();

	LiveLinkHubClient->OnStaticDataReceived_AnyThread().AddSP(this, &FLiveLinkHub::OnStaticDataReceived_AnyThread);
	LiveLinkHubClient->OnFrameDataReceived_AnyThread().AddSP(this, &FLiveLinkHub::OnFrameDataReceived_AnyThread);
	LiveLinkHubClient->OnSubjectMarkedPendingKill_AnyThread().AddSP(this, &FLiveLinkHub::OnSubjectMarkedPendingKill_AnyThread);

	RegisterLiveLinkHubSettings();

	PlaybackController->Start();

	GIsRunning = true;
}

FLiveLinkHub::~FLiveLinkHub()
{
	UnregisterLiveLinkHubSettings();

	RecordingController.Reset();
	PlaybackController.Reset();

	LiveLinkHubClient->OnSubjectMarkedPendingKill_AnyThread().RemoveAll(this);
	LiveLinkHubClient->OnFrameDataReceived_AnyThread().RemoveAll(this);
	LiveLinkHubClient->OnStaticDataReceived_AnyThread().RemoveAll(this);

	IModularFeatures::Get().UnregisterModularFeature(ILiveLinkClient::ModularFeatureName, LiveLinkHubClient.Get());
}

bool FLiveLinkHub::IsInPlayback() const
{
	return PlaybackController->IsInPlayback();
}

bool FLiveLinkHub::IsRecording() const
{
	return RecordingController->IsRecording();
}
void FLiveLinkHub::Tick()
{
	LiveLinkHubClient->Tick();
}

TSharedRef<SWindow> FLiveLinkHub::GetRootWindow() const
{
	return WindowController->GetRootWindow().ToSharedRef();
}

TSharedPtr<FLiveLinkHubProvider> FLiveLinkHub::GetLiveLinkProvider() const
{
	return LiveLinkProvider;
}

TSharedPtr<FLiveLinkHubClientsController> FLiveLinkHub::GetClientsController() const
{
	return ClientsController;
}

TSharedPtr<ILiveLinkHubSessionManager> FLiveLinkHub::GetSessionManager() const
{
	return SessionManager;
}

TSharedPtr<FLiveLinkHubRecordingController> FLiveLinkHub::GetRecordingController() const
{
	return RecordingController;
}

TSharedPtr<FLiveLinkHubRecordingListController> FLiveLinkHub::GetRecordingListController() const
{
	return RecordingListController;
}

TSharedPtr<FLiveLinkHubPlaybackController> FLiveLinkHub::GetPlaybackController() const
{
	return PlaybackController;
}

void FLiveLinkHub::OnStaticDataReceived_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, TSubclassOf<ULiveLinkRole> InRole, const FLiveLinkStaticDataStruct& InStaticDataStruct) const
{
	if (RecordingController->IsRecording())
	{
		RecordingController->RecordStaticData(InSubjectKey, InRole, InStaticDataStruct);
	}
}

void FLiveLinkHub::OnFrameDataReceived_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, const FLiveLinkFrameDataStruct& InFrameDataStruct) const
{
	if (RecordingController->IsRecording())
	{
		RecordingController->RecordFrameData(InSubjectKey, InFrameDataStruct);
	}
}

void FLiveLinkHub::OnSubjectMarkedPendingKill_AnyThread(const FLiveLinkSubjectKey& InSubjectKey) const
{
	UE_LOG(LogLiveLinkHub, Verbose, TEXT("Removed subject %s"), *InSubjectKey.SubjectName.ToString());

	// Send an update to connected clients as well.
	const FName OverridenName = LiveLinkHubClient->GetRebroadcastName(InSubjectKey);

	// Note: We send a RemoveSubject message to connected clients when the subject is marked pending kill in order to process this message in the right order.
	// If we were to send a RemoveSubject message after the OnSubjectRemoved delegate, it could cause our RemoveSubject message to be sent out of order.
	LiveLinkProvider->RemoveSubject(OverridenName);
}

void FLiveLinkHub::BindCommands()
{
	const FLiveLinkHubCommands& Commands = FLiveLinkHubCommands::Get();
	CommandList->MapAction(Commands.NewConfig, FExecuteAction::CreateSP(this, &FLiveLinkHub::NewConfig));
	CommandList->MapAction(Commands.OpenConfig, FExecuteAction::CreateSP(this, &FLiveLinkHub::OpenConfig));
	CommandList->MapAction(Commands.SaveConfigAs, FExecuteAction::CreateSP(this, &FLiveLinkHub::SaveConfigAs));
	CommandList->MapAction(Commands.SaveConfig, FExecuteAction::CreateSP(this, &FLiveLinkHub::SaveConfig),
		FCanExecuteAction::CreateSP(this, &FLiveLinkHub::CanSaveConfig));
}

void FLiveLinkHub::NewConfig()
{
	SessionManager->NewSession();
}

void FLiveLinkHub::SaveConfigAs()
{
	SessionManager->SaveSessionAs();
}

bool FLiveLinkHub::CanSaveConfig() const
{
	return SessionManager->CanSaveCurrentSession();
}

void FLiveLinkHub::SaveConfig()
{
	SessionManager->SaveCurrentSession();
}

void FLiveLinkHub::OpenConfig()
{
	SessionManager->RestoreSession();
}

void FLiveLinkHub::RegisterLiveLinkHubSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Editor", "Plugins", "Live Link",
			LOCTEXT("EditorSettingsName", "Live Link"),
			LOCTEXT("EditorSettingsDescription", "Configure Live Link."),
			GetMutableDefault<ULiveLinkEditorSettings>()
		);

		SettingsModule->RegisterSettings("Project", "Plugins", "Live Link",
			LOCTEXT("LiveLinkSettingsName", "Live Link"),
			LOCTEXT("LiveLinkDescription", "Configure Live Link."),
			GetMutableDefault<ULiveLinkSettings>()
		);

		SettingsModule->RegisterSettings("Project", "Plugins", "Live Link Hub",
			LOCTEXT("LiveLinkHubSettingsName", "Live Link Hub"),
			LOCTEXT("LiveLinkHubDescription", "Configure Live Link Hub."),
			GetMutableDefault<ULiveLinkHubSettings>()
		);
	}
}

void FLiveLinkHub::UnregisterLiveLinkHubSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Editor", "Plugins", "Live Link");
		SettingsModule->UnregisterSettings("Project", "Plugins", "Live Link");
		SettingsModule->UnregisterSettings("Project", "Plugins", "Live Link Hub");
	}
}

#undef LOCTEXT_NAMESPACE /*LiveLinkHub*/
