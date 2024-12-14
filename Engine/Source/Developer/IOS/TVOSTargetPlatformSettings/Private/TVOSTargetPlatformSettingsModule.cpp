// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOSTargetPlatformSettings.h"
#include "Interfaces/ITargetPlatformSettingsModule.h"
#include "Modules/ModuleManager.h"

/**
 * Module for TVOS as a target platform settings
 */
class FTVOSTargetPlatformSettingsModule	: public ITargetPlatformSettingsModule
{
public:

	virtual void GetTargetPlatformSettings(TArray<ITargetPlatformSettings*>& TargetPlatforms) override
	{
		TargetPlatforms.Add(new FIOSTargetPlatformSettings(true, false));
	}
};


IMPLEMENT_MODULE(FTVOSTargetPlatformSettingsModule, TVOSTargetPlatformSettings);
