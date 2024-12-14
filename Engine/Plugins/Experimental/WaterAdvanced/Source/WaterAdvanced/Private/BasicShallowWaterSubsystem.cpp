// Copyright Epic Games, Inc. All Rights Reserved.

#include "BasicShallowWaterSubsystem.h"

UBasicShallowWaterSubsystem::UBasicShallowWaterSubsystem()
{
}

bool UBasicShallowWaterSubsystem::IsShallowWaterAllowedToInitialize() const
{
	TObjectPtr<UShallowWaterSettings> TmpSettings = GetMutableDefault<UShallowWaterSettings>();
	if (TmpSettings)
	{
		return TmpSettings->UseDefaultShallowWaterSubsystem;
	}

	return false;
}