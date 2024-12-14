// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDJointConstraintVisualizationSettings.h"

#include "ChaosVDSettingsManager.h"


void UChaosVDJointConstraintsVisualizationSettings::SetDataVisualizationFlags(EChaosVDJointsDataVisualizationFlags NewFlags)
{
	if (UChaosVDJointConstraintsVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDJointConstraintsVisualizationSettings>())
	{
		Settings->GlobalJointsDataVisualizationFlags = static_cast<uint32>(NewFlags);
		Settings->BroadcastSettingsChanged();
	}
}

EChaosVDJointsDataVisualizationFlags UChaosVDJointConstraintsVisualizationSettings::GetDataVisualizationFlags()
{
	if (UChaosVDJointConstraintsVisualizationSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDJointConstraintsVisualizationSettings>())
	{
		return static_cast<EChaosVDJointsDataVisualizationFlags>(Settings->GlobalJointsDataVisualizationFlags);
	}

	return EChaosVDJointsDataVisualizationFlags::None;
}

