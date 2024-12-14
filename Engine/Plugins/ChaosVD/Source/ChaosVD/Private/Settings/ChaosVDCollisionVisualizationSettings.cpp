// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDCollisionVisualizationSettings.h"

#include "ChaosVDSettingsManager.h"


void UChaosVDCollisionDataVisualizationSettings::SetDataVisualizationFlags(EChaosVDCollisionVisualizationFlags NewFlags)
{
	if (UChaosVDCollisionDataVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCollisionDataVisualizationSettings>())
	{
		Settings->CollisionDataVisualizationFlags = static_cast<uint32>(NewFlags);
		Settings->BroadcastSettingsChanged();
	}
}

EChaosVDCollisionVisualizationFlags UChaosVDCollisionDataVisualizationSettings::GetDataVisualizationFlags()
{
	if (UChaosVDCollisionDataVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCollisionDataVisualizationSettings>())
	{
		return static_cast<EChaosVDCollisionVisualizationFlags>(Settings->CollisionDataVisualizationFlags) ;
	}

	return EChaosVDCollisionVisualizationFlags::None;
}

