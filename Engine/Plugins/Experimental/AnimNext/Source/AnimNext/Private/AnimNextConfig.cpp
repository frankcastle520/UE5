// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextConfig.h"

#if WITH_EDITOR

void UAnimNextConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	SaveConfig();
}

#endif