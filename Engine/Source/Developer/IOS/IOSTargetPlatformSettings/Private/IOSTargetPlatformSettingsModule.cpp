// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOSTargetPlatformSettings.h"
#include "Interfaces/ITargetPlatformSettingsModule.h"
#include "Modules/ModuleManager.h"

/**
 * Module for iOS as a target platform settings
 */
class FIOSTargetPlatformSettingsModule : public ITargetPlatformSettingsModule
{
public:
	virtual void GetTargetPlatformSettings(TArray<ITargetPlatformSettings*>& TargetPlatforms) override
	{
		TargetPlatforms.Add(new FIOSTargetPlatformSettings(false, false));
	}
};


IMPLEMENT_MODULE(FIOSTargetPlatformSettingsModule, IOSTargetPlatformSettings);
