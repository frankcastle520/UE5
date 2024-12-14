// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interfaces/ITargetPlatformModule.h"
#include "Modules/ModuleManager.h"
#include "Common/TargetPlatformBase.h"

/**
 * Module for TVOS as a target platform
 */
class FVisionOSTargetPlatformModule	: public ITargetPlatformModule
{
public:

	virtual void GetTargetPlatforms(TArray<ITargetPlatform*>& TargetPlatforms) override
	{
	}

	virtual void GetTargetPlatforms(TArray<ITargetPlatform*>& TargetPlatforms, TArray<ITargetPlatformSettings*> TargetPlatformSettings, TArray<ITargetPlatformControls*> TargetPlatformControls)
	{
		for (ITargetPlatformControls* TargetPlatformControlsIt : TargetPlatformControls)
		{
			TargetPlatforms.Add(new FTargetPlatformMerged(TargetPlatformControlsIt->GetTargetPlatformSettings(), TargetPlatformControlsIt));
		}
	}
};


IMPLEMENT_MODULE(FVisionOSTargetPlatformModule, VisionOSTargetPlatform);
