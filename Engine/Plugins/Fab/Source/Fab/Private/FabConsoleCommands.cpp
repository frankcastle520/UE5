// Copyright Epic Games, Inc. All Rights Reserved.

#include "FabAuthentication.h"
#include "FabBrowser.h"
#include "FabLog.h"
#include "FabSettings.h"

#include "HAL/IConsoleManager.h"
#include "Utilities/FabAssetsCache.h"

static FAutoConsoleCommand ConsoleCmd_FabShowSettings(
	TEXT("Fab.ShowSettings"),
	TEXT("Display the Fab settings window"),
	FConsoleCommandDelegate::CreateStatic(&FFabBrowser::ShowSettings)
);

static FAutoConsoleCommand ConsoleCmd_FabLogout(
	TEXT("Fab.Logout"),
	TEXT("Trigger a manual logout for Fab plugin"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FabAuthentication::DeletePersistentAuth();
	})
);

static FAutoConsoleCommand ConsoleCmd_FabLogin(
	TEXT("Fab.Login"),
	TEXT("Trigger a manual login for Fab plugin"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FabAuthentication::LoginUsingAccountPortal();
	})
);

static FAutoConsoleCommand ConsoleCmd_FabClearCache(
	TEXT("Fab.ClearCache"),
	TEXT("Clear download cache for Fab plugin"),
	FConsoleCommandDelegate::CreateStatic(&FFabAssetsCache::ClearCache)
);

static FAutoConsoleCommand ConsoleCmd_FabSetEnvironment(
	TEXT("Fab.SetEnvironment"),
	TEXT("Set Fab plugin environment"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.IsEmpty())
		{
			FAB_LOG("Need to provide a valid environment arg");
			return;
		}
		
		const FString Environment = Args[0];
		UFabSettings* FabSettings = GetMutableDefault<UFabSettings>();
		if (Environment == "prod")
		{
			FabSettings->Environment = EFabEnvironment::Prod;
		}
		else if (Environment == "gamedev")
		{
			FabSettings->Environment = EFabEnvironment::Gamedev;
		}
		else if (Environment == "test")
		{
			FabSettings->Environment = EFabEnvironment::Test;
		}
		
		FabAuthentication::DeletePersistentAuth();
		FabSettings->SaveConfig();
	})
);
