// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkHubSession.h"

#include "Config/LiveLinkHubFileUtilities.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Engine/Engine.h"
#include "HAL/CriticalSection.h"
#include "IDesktopPlatform.h"
#include "LiveLinkHubClient.h"
#include "LiveLinkHubLog.h"
#include "LiveLinkSourceSettings.h"
#include "Settings/LiveLinkHubSettings.h"

#define LOCTEXT_NAMESPACE "LiveLinkHub.SessionManager"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnActiveSessionChanged, const TSharedRef<ILiveLinkHubSession>& /**Active Session*/);

class ILiveLinkHubSessionManager
{
public:
	virtual ~ILiveLinkHubSessionManager() = default;

	/** Delegate called when a UE client is added to the current session, enabling it to receive data from the hub. */
	virtual FOnClientAddedToSession& OnClientAddedToSession() = 0;

	/** Delegate called when a UE client is removed from the current session, returning it to the list of discovered clients. */
	virtual FOnClientRemovedFromSession& OnClientRemovedFromSession() = 0;

	/** Delegate called when the active session changes, which will change the list of sources, subjects and clients. */
	virtual FOnActiveSessionChanged& OnActiveSessionChanged() = 0;

	/** Get the current session, which holds information about which sources, subjects and clients that should be enabled in the hub at the moment. */
	virtual TSharedPtr<ILiveLinkHubSession> GetCurrentSession() const = 0;

	/** Clear out the current session data and stat a new empty session. */
	virtual void NewSession() = 0;

	/** Prompt the user to save the current session in a given directory. */
	virtual void SaveSessionAs() = 0;

	/** Prompt the user to pick a session file to restore. */
	virtual void RestoreSession() = 0;

	/** Save the current session. If not path is specified, the last save path will be used. */
	virtual void SaveCurrentSession(const FString& SavePath = TEXT("")) = 0;

	/** Returns whether the current session has as already been saved to disk before. */
	virtual bool CanSaveCurrentSession() const = 0;

	/** Returns the last used config path. */
	virtual const FString& GetLastConfigPath() const = 0;
};

class FLiveLinkHubSessionManager : public ILiveLinkHubSessionManager
{
public:
	FLiveLinkHubSessionManager()
	{
		FScopeLock Lock(&CurrentSessionCS);
		CurrentSession = MakeShared<FLiveLinkHubSession>(OnClientAddedToSessionDelegate, OnClientRemovedFromSessionDelegate);
	}

	virtual ~FLiveLinkHubSessionManager() override = default;

	//~ Begin LiveLinkHubSessionManager
	virtual FOnClientAddedToSession& OnClientAddedToSession() override
	{
		check(IsInGameThread());
		return OnClientAddedToSessionDelegate;
	}

	virtual FOnClientRemovedFromSession& OnClientRemovedFromSession() override
	{
		check(IsInGameThread());
		return OnClientRemovedFromSessionDelegate;
	}

	virtual FOnActiveSessionChanged& OnActiveSessionChanged() override
	{
		check(IsInGameThread());
		return OnActiveSessionChangedDelegate;
	}

	virtual void NewSession() override
	{
		ClearSession();
		LastConfigPath.Empty();
	}

	virtual void SaveSessionAs() override
	{
		const FString FileDescription = UE::LiveLinkHub::FileUtilities::Private::ConfigDescription;
		const FString Extensions = UE::LiveLinkHub::FileUtilities::Private::ConfigExtension;
		const FString FileTypes = FString::Printf(TEXT("%s (*.%s)|*.%s"), *FileDescription, *Extensions, *Extensions);

		const FString DefaultFile = UE::LiveLinkHub::FileUtilities::Private::ConfigDefaultFileName;

		TArray<FString> SaveFileNames;

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		const bool bFileSelected = DesktopPlatform->SaveFileDialog(
			ParentWindowWindowHandle,
			LOCTEXT("LiveLinkHubSaveAsTitle", "Save As").ToString(),
			FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_SAVE),
			DefaultFile,
			FileTypes,
			EFileDialogFlags::None,
			SaveFileNames);

		if (bFileSelected && SaveFileNames.Num() > 0)
		{
			SaveCurrentSession(SaveFileNames[0]);
		}
	}

	virtual TSharedPtr<ILiveLinkHubSession> GetCurrentSession() const override
	{
		FScopeLock Lock(&CurrentSessionCS);
		return CurrentSession;
	}

	virtual void SaveCurrentSession(const FString& SavePath = TEXT("")) override
	{
		if (SavePath.IsEmpty() && LastConfigPath.IsEmpty())
		{
			return;
		}

		FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
		TSharedPtr<FLiveLinkHubProvider> LiveLinkProvider = LiveLinkHubModule.GetLiveLinkProvider();
		FLiveLinkHubClient* LiveLinkHubClient = static_cast<FLiveLinkHubClient*>(&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName));

		check(LiveLinkProvider);
		check(LiveLinkHubClient);

		TSharedPtr<FLiveLinkHubSession> CurrentSessionPtr;
		{
			FScopeLock Lock(&CurrentSessionCS);
			CurrentSessionPtr = CurrentSession;
		}

		ULiveLinkHubSessionData* LiveLinkHubSessionData = CastChecked<ULiveLinkHubSessionData>(StaticCastSharedPtr<FLiveLinkHubSession>(CurrentSessionPtr)->SessionData.Get());

		LiveLinkHubSessionData->Sources.Empty();
		LiveLinkHubSessionData->Subjects.Empty();
		
		TArray<FGuid> SourceGuids = LiveLinkHubClient->GetSources();
		for (const FGuid& SourceGuid : SourceGuids)
		{
			LiveLinkHubSessionData->Sources.Add(LiveLinkHubClient->GetSourcePreset(SourceGuid, nullptr));
		}

		TArray<FLiveLinkSubjectKey> Subjects = LiveLinkHubClient->GetSubjects(true, true);
		for (const FLiveLinkSubjectKey& Subject : Subjects)
		{
			LiveLinkHubSessionData->Subjects.Add(LiveLinkHubClient->GetSubjectPreset(Subject, nullptr));
		}

		const TMap<FLiveLinkHubClientId, FLiveLinkHubUEClientInfo>& ClientMap = LiveLinkProvider->GetClientsMap();

		for (const TTuple<FLiveLinkHubClientId, FLiveLinkHubUEClientInfo>& ClientKeyVal : ClientMap)
		{
			LiveLinkHubSessionData->Clients.Add(ClientKeyVal.Value);
		}

		const FLiveLinkHubTimecodeSettings& TimecodeSettings = LiveLinkProvider->GetTimecodeSettings();
		LiveLinkHubSessionData->TimecodeSettings = TimecodeSettings;
		
		if (!SavePath.IsEmpty())
		{
			LastConfigPath = SavePath;
		}
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_SAVE, FPaths::GetPath(LastConfigPath));

		UE::LiveLinkHub::FileUtilities::Private::SaveConfig(LiveLinkHubSessionData, LastConfigPath);
	}

	virtual void RestoreSession() override
	{
		const FString FileDescription = UE::LiveLinkHub::FileUtilities::Private::ConfigDescription;
		const FString Extensions = UE::LiveLinkHub::FileUtilities::Private::ConfigExtension;
		const FString FileTypes = FString::Printf(TEXT("%s (*.%s)|*.%s"), *FileDescription, *Extensions, *Extensions);

		const FString DefaultFile = UE::LiveLinkHub::FileUtilities::Private::ConfigDefaultFileName;

		TArray<FString> OpenFileNames;

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const bool bFileSelected = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			LOCTEXT("LiveLinkHubOpenTitle", "Open").ToString(),
			FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_OPEN),
			DefaultFile,
			FileTypes,
			EFileDialogFlags::None,
			OpenFileNames);

		if (bFileSelected && OpenFileNames.Num() > 0)
		{
			// Certain sources may take time to clean up. If they don't complete in time then the new config being loaded may not create
			// duplicate sources correctly. There should be errors in the logs of the sources that failed to remove or were unable to be added.
			constexpr bool bWaitForSourceRemoval = true;
			ClearSession(bWaitForSourceRemoval);
			RestoreSession(OpenFileNames[0]);
		}
	}

	virtual bool CanSaveCurrentSession() const override
	{
		return !LastConfigPath.IsEmpty();
	}

	virtual const FString& GetLastConfigPath() const override
	{
		return LastConfigPath;
	}
	
	//~ End LiveLinkHubSessionManager

private:
	/** Load a session from disk and restore its content. */
	void RestoreSession(const FString& Path)
	{
		LastConfigPath = Path;
		FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_OPEN, FPaths::GetPath(LastConfigPath));

		if (ULiveLinkHubSessionData* SessionData = UE::LiveLinkHub::FileUtilities::Private::LoadConfig(LastConfigPath))
		{
			FLiveLinkHubClient* LiveLinkHubClient = static_cast<FLiveLinkHubClient*>(&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName));

			const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
			const TSharedPtr<FLiveLinkHubProvider> LiveLinkProvider = LiveLinkHubModule.GetLiveLinkProvider();

			check(LiveLinkHubClient);
			check(LiveLinkProvider);

			if (SessionData)
			{
				for (const FLiveLinkSourcePreset& SourcePreset : SessionData->Sources)
				{
					LiveLinkHubClient->CreateSource(SourcePreset);
					// Ensure stored source settings persist. CreateSource will call Source->InitializeSettings, which passes in
					// a mutable settings object. Some sources may set "default" values on the settings object overriding the
					// saved values from the config. We want to prevent that behavior, but we still have to call InitializeSettings, because
					// other sources may set internal values based on the current settings' values, which is behavior we want to keep.
					if (ULiveLinkSourceSettings* PresetSettings = SourcePreset.Settings.Get())
					{
						if (ULiveLinkSourceSettings* CreatedSettings = LiveLinkHubClient->GetSourceSettings(SourcePreset.Guid))
						{
							UEngine::FCopyPropertiesForUnrelatedObjectsParams CopyParams;
							CopyParams.bDoDelta = false;
							UEngine::CopyPropertiesForUnrelatedObjects(PresetSettings, CreatedSettings, CopyParams);
						}
					}
				}

				for (const FLiveLinkSubjectPreset& SubjectPreset : SessionData->Subjects)
				{
					LiveLinkHubClient->CreateSubject(SubjectPreset);
				}
			}

			SessionData->TimecodeSettings.AssignTimecodeSettingsAsProviderToEngine();
			LiveLinkProvider->SetTimecodeSettings(SessionData->TimecodeSettings);

			TSharedPtr<FLiveLinkHubSession> CurrentSessionPtr;
			{
				FScopeLock Lock(&CurrentSessionCS);
				CurrentSessionPtr = CurrentSession = MakeShared<FLiveLinkHubSession>(SessionData, OnClientAddedToSessionDelegate, OnClientRemovedFromSessionDelegate);
			}

			for (FLiveLinkHubUEClientInfo& Client : SessionData->Clients)
			{
				CurrentSessionPtr->AddRestoredClient(Client);
			}
			OnActiveSessionChangedDelegate.Broadcast(CurrentSessionPtr.ToSharedRef());
		}
	}

	/** Clear the hub data contained in the current session, resetting the hub to its default state. */
	void ClearSession(bool bWaitForSourceRemoval = false)
	{
		FLiveLinkHubClient* LiveLinkHubClient = static_cast<FLiveLinkHubClient*>(&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName));
		check(LiveLinkHubClient);
		
		const float TimeToWaitForRemoval = bWaitForSourceRemoval ? GetDefault<ULiveLinkHubSettings>()->SourceMaxCleanupTime : 0.f;
		const bool bRemovedAllSources = LiveLinkHubClient->RemoveAllSourcesWithTimeout(TimeToWaitForRemoval);
		
		if (!bRemovedAllSources && bWaitForSourceRemoval)
		{
			UE_LOG(LogLiveLinkHub, Warning, TEXT("Could not remove all existing sources in time. Sources may still be getting cleaned up."));
		}
		
		TSharedPtr<FLiveLinkHubSession> CurrentSessionPtr;
		{
			FScopeLock Lock(&CurrentSessionCS);
			CurrentSessionPtr = CurrentSession = MakeShared<FLiveLinkHubSession>(OnClientAddedToSessionDelegate, OnClientRemovedFromSessionDelegate);
		}

		OnActiveSessionChangedDelegate.Broadcast(CurrentSessionPtr.ToSharedRef());
	}

private:
	/** Session that holds the current configuration of the hub (Clients, sources, subjects). */
	TSharedPtr<FLiveLinkHubSession> CurrentSession;
	/** Last path where we saved a session config file. */
	FString LastConfigPath;
	/** Delegate triggered when a client is added to the current session. */
	FOnClientAddedToSession OnClientAddedToSessionDelegate;
	/** Delegate triggered when a client is removed from the current session. */
	FOnClientRemovedFromSession OnClientRemovedFromSessionDelegate;
	/** Delegate triggered when the current session is changed. */
	FOnActiveSessionChanged OnActiveSessionChangedDelegate;
	/** Critical section used to synchronize access to the current session. */
	mutable FCriticalSection CurrentSessionCS;
};

#undef LOCTEXT_NAMESPACE /*LiveLinkHub.SessionManager*/
