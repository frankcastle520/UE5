// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once 

/*------------------------------------------------------------------------------------
IWindowsTargetPlatformSettingsModule interface
------------------------------------------------------------------------------------*/

#include "CoreMinimal.h"
#include "Interfaces/ITargetPlatformSettingsModule.h"
#include "GenericWindowsTargetPlatformSettings.h"

class IWindowsTargetPlatformSettingsModule : public ITargetPlatformSettingsModule
{
public:
	virtual void GetPlatformSettingsMaps(TMap<FString, ITargetPlatformSettings*>& OutMap) = 0;
	virtual ITargetPlatformSettings* GetCookedEditorPlatformSettings() = 0;
	virtual ITargetPlatformSettings* GetCookedCookerPlatformSettings() = 0;
};
