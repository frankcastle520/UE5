// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "LiveLinkHubEditorSettings.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"


namespace UE::LiveLinkHubLauncherUtils
{
	struct FInstalledApp
	{
		/** Location of the installed app. */
		FString InstallLocation;

		/** Namespace of the app. */
		FString NamespaceId;

		/** Id of the app. */
		FString ItemId;

		/** Unique ID for the app on the EGS. */
		FString ArtifactId;

		/** Version of the app. For LiveLinkHub this will correspond to a CL number.  */
		FString AppVersion;

		/** The apps' internal name. Usually matches the ArtifactId except if the app was using a legacy publishing workflow.  */
		FString AppName;
	};

	/** Gather all the installed apps from the Epic Games Launcher. */
	static bool FindLiveLinkHubInstallation(FInstalledApp& OutLiveLinkHubInfo)
	{
		FString InstalledListFile = FString(FPlatformProcess::ApplicationSettingsDir()) / TEXT("UnrealEngineLauncher/LauncherInstalled.dat");

		FString InstalledText;
		if (FFileHelper::LoadFileToString(InstalledText, *InstalledListFile))
		{
			// Deserialize the object
			TSharedPtr<FJsonObject> RootObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InstalledText);
			if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
			{
				// Parse the list of installations
				TArray<TSharedPtr<FJsonValue>> InstallationList = RootObject->GetArrayField(TEXT("InstallationList"));
				for (int32 Idx = 0; Idx < InstallationList.Num(); Idx++)
				{
					FInstalledApp InstalledApp;

					TSharedPtr<FJsonObject> InstallationItem = InstallationList[Idx]->AsObject();
					InstalledApp.AppName = InstallationItem->GetStringField(TEXT("AppName"));

					if (InstalledApp.AppName == GetDefault<ULiveLinkHubEditorSettings>()->LiveLinkHubAppName)
					{
						InstalledApp.InstallLocation = InstallationItem->GetStringField(TEXT("InstallLocation"));

						if (!InstalledApp.InstallLocation.Len())
						{
							// Shouldn't happen in theory, but just to be safe. 
							// Doing a continue here instead of returning in case there were somehow multiple LLH installations.
							continue;
						}

						const FString& TargetVersion = GetDefault<ULiveLinkHubEditorSettings>()->LiveLinkHubTargetVersion;
						InstalledApp.AppVersion = InstallationItem->GetStringField(TEXT("AppVersion"));

						if (!TargetVersion.IsEmpty() && InstalledApp.AppVersion != TargetVersion)
						{
							// If we target a specific version and it doesn't match the installed app, ignore it.
							continue;
						}

						InstalledApp.NamespaceId = InstallationItem->GetStringField(TEXT("NamespaceId"));
						InstalledApp.ItemId = InstallationItem->GetStringField(TEXT("ItemId"));
						InstalledApp.ArtifactId = InstallationItem->GetStringField(TEXT("ArtifactId"));


						OutLiveLinkHubInfo = MoveTemp(InstalledApp);
						return true;
					}
				}
			}
		}

		return false;
	}
}