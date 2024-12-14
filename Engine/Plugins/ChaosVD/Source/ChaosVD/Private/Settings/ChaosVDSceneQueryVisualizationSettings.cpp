// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDSceneQueryVisualizationSettings.h"

#include "ChaosVDSettingsManager.h"


void UChaosVDSceneQueriesVisualizationSettings::SetDataVisualizationFlags(EChaosVDSceneQueryVisualizationFlags NewFlags)
{
	if (UChaosVDSceneQueriesVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDSceneQueriesVisualizationSettings>())
	{
		Settings->GlobalSceneQueriesVisualizationFlags = static_cast<uint32>(NewFlags);
		Settings->BroadcastSettingsChanged();
	}
}

EChaosVDSceneQueryVisualizationFlags UChaosVDSceneQueriesVisualizationSettings::GetDataVisualizationFlags()
{
	if (UChaosVDSceneQueriesVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDSceneQueriesVisualizationSettings>())
	{
		return static_cast<EChaosVDSceneQueryVisualizationFlags>(Settings->GlobalSceneQueriesVisualizationFlags);
	}

	return EChaosVDSceneQueryVisualizationFlags::None;
}
