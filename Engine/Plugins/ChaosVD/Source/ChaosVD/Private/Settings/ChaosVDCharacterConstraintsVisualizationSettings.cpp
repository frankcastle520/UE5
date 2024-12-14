// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDCharacterConstraintsVisualizationSettings.h"

#include "ChaosVDSettingsManager.h"


void UChaosVDCharacterConstraintsVisualizationSettings::SetDataVisualizationFlags(EChaosVDCharacterGroundConstraintDataVisualizationFlags NewFlags)
{
	if (UChaosVDCharacterConstraintsVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCharacterConstraintsVisualizationSettings>())
	{
		Settings->GlobalCharacterGroundConstraintDataVisualizationFlags = static_cast<uint32>(NewFlags);
		Settings->BroadcastSettingsChanged();
	}
}

EChaosVDCharacterGroundConstraintDataVisualizationFlags UChaosVDCharacterConstraintsVisualizationSettings::GetDataVisualizationFlags()
{
	if (UChaosVDCharacterConstraintsVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCharacterConstraintsVisualizationSettings>())
	{
		return static_cast<EChaosVDCharacterGroundConstraintDataVisualizationFlags>(Settings->GlobalCharacterGroundConstraintDataVisualizationFlags);
	}

	return EChaosVDCharacterGroundConstraintDataVisualizationFlags::None;
}
