// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FLinuxArm64TargetPlatformControlsModule.cpp: Implements the FLinuxArm64TargetPlatformControlsModule class.
=============================================================================*/

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Interfaces/ITargetPlatformControlsModule.h"

#include "LinuxTargetPlatformSettings.h"
#include "LinuxTargetPlatformControls.h"
#include "ILinuxArm64TargetPlatformSettingsModule.h"

/**
 * Module for the Linux target platforms controls
 */
class FLinuxArm64TargetPlatformControlsModule
	: public ITargetPlatformControlsModule
{
public:
	virtual void GetTargetPlatformControls(TArray<ITargetPlatformControls*>& TargetPlatforms, FName& PlatformSettingsModuleName) override
	{
		TMap<FString, ITargetPlatformSettings*> PlatformNameToPlatformSettings;
		ILinuxArm64TargetPlatformSettingsModule* ModuleSettings = FModuleManager::GetModulePtr<ILinuxArm64TargetPlatformSettingsModule>(PlatformSettingsModuleName);
		if (ModuleSettings != nullptr)
		{
			TMap<FString, ITargetPlatformSettings*> OutMap;
			ModuleSettings->GetPlatformSettingsMaps(OutMap);
			ITargetPlatformControls* GameTP = new TLinuxTargetPlatformControls<FLinuxPlatformProperties<false, false, false, true>>(OutMap[FLinuxPlatformProperties<false, false, false, true>::PlatformName()]);
			ITargetPlatformControls* ServerTP = new TLinuxTargetPlatformControls<FLinuxPlatformProperties<false, true, false, true>>(OutMap[FLinuxPlatformProperties<false, true, false, true>::PlatformName()]);
			ITargetPlatformControls* ClientTP = new TLinuxTargetPlatformControls<FLinuxPlatformProperties<false, false, true, true>>(OutMap[FLinuxPlatformProperties<false, false, true, true>::PlatformName()]);
			TargetPlatforms.Add(GameTP);
			TargetPlatforms.Add(ServerTP);
			TargetPlatforms.Add(ClientTP);
		}
	}
};

IMPLEMENT_MODULE(FLinuxArm64TargetPlatformControlsModule, LinuxArm64TargetPlatformControls);
