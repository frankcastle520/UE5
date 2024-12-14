// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDProjectSettings.h"

#if WITH_EDITOR
void UUsdProjectSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UDeveloperSettings::PostEditChangeProperty(PropertyChangedEvent);

	// Allows settings to be applied even if the properties were set via utility blueprints/Python
	SaveConfig();
}
#endif	  // WITH_EDITOR
