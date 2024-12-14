// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinuxArm64TargetPlatformSettingsModule.cpp: Implements the FLinuxArm64TargetPlatformSettingsModule class.
=============================================================================*/

#include "Modules/ModuleManager.h"
#include "ILinuxArm64TargetPlatformSettingsModule.h"
#include "LinuxTargetPlatformSettings.h"


/**
 * Module for the Linux target platforms settings
 */
class FLinuxArm64TargetPlatformSettingsModule
	: public ILinuxArm64TargetPlatformSettingsModule
{
public:

	/** Destructor. */
	~FLinuxArm64TargetPlatformSettingsModule()
	{

	}

	virtual void GetTargetPlatformSettings(TArray<ITargetPlatformSettings*>& TargetPlatforms) override
	{
		// NoEditor TP
		ITargetPlatformSettings* NoEditorTP = new TLinuxTargetPlatformSettings<FLinuxPlatformProperties<false, false, false, true>>();
		TargetPlatforms.Add(NoEditorTP);
		PlatformNameToPlatformSettings.Add(FLinuxPlatformProperties<false, false, false, true>::PlatformName(), NoEditorTP);
		// Server TP
		ITargetPlatformSettings* ServerTP = new TLinuxTargetPlatformSettings<FLinuxPlatformProperties<false, true, false, true>>();
		TargetPlatforms.Add(ServerTP);
		PlatformNameToPlatformSettings.Add(FLinuxPlatformProperties<false, true, false, true>::PlatformName(), ServerTP);
		// Client TP
		ITargetPlatformSettings* ClientTP = new TLinuxTargetPlatformSettings<FLinuxPlatformProperties<false, false, true, true>>();
		TargetPlatforms.Add(ClientTP);
		PlatformNameToPlatformSettings.Add(FLinuxPlatformProperties<false, false, true, true>::PlatformName(), ClientTP);
	}

	virtual void GetPlatformSettingsMaps(TMap<FString, ITargetPlatformSettings*>& OutMap) override
	{
		OutMap = PlatformNameToPlatformSettings;
	}

private:
	TMap<FString, ITargetPlatformSettings*> PlatformNameToPlatformSettings;

};

IMPLEMENT_MODULE(FLinuxArm64TargetPlatformSettingsModule, LinuxArm64TargetPlatformSettings);