// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldBrowserConfig.h"

TObjectPtr<UWorldBrowserConfig> UWorldBrowserConfig::Instance = nullptr;

void UWorldBrowserConfig::Initialize()
{
	if(!Instance)
	{
		Instance = NewObject<UWorldBrowserConfig>(); 
		Instance->AddToRoot();
		Instance->LoadEditorConfig();
	}
}