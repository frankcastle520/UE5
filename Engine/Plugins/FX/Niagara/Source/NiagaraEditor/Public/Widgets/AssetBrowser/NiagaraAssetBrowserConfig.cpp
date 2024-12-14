// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraAssetBrowserConfig.h"

TObjectPtr<UNiagaraAssetBrowserConfig> UNiagaraAssetBrowserConfig::Instance = nullptr;

void UNiagaraAssetBrowserConfig::Initialize()
{
	if(!Instance)
	{
		Instance = NewObject<UNiagaraAssetBrowserConfig>(); 
		Instance->AddToRoot();
	}
}

void UNiagaraAssetBrowserConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnPropertyChangedDelegate.Broadcast(PropertyChangedEvent);
	SaveEditorConfig();
}
