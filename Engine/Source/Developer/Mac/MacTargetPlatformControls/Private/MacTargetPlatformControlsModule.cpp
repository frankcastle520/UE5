// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "GenericMacTargetPlatformControls.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatformControlsModule.h"
#include "Modules/ModuleManager.h"
#include "IMacTargetPlatformSettingsModule.h"

#define LOCTEXT_NAMESPACE "FMacTargetPlatformControlsModule"

/**
 * Module for Mac as a target platform controls
 */
class FMacTargetPlatformControlsModule
	: public ITargetPlatformControlsModule
{
public:
	virtual void GetTargetPlatformControls(TArray<ITargetPlatformControls*>& TargetPlatforms, FName& PlatformSettingsModuleName) override
	{
		TMap<FString, ITargetPlatformSettings*> PlatformNameToPlatformSettings;
		IMacTargetPlatformSettingsModule* ModuleSettings = FModuleManager::GetModulePtr<IMacTargetPlatformSettingsModule>(PlatformSettingsModuleName);
		if (ModuleSettings != nullptr)
		{
			TMap<FString, ITargetPlatformSettings*> OutMap;
			ModuleSettings->GetPlatformSettingsMaps(OutMap);
			ITargetPlatformControls* GameTP = new TGenericMacTargetPlatformControls<false, false, false>(OutMap[FMacPlatformProperties<false, false, false>::PlatformName()]);
			ITargetPlatformControls* EditorTP = new TGenericMacTargetPlatformControls<true, false, false>(OutMap[FMacPlatformProperties<true, false, false>::PlatformName()]);
			ITargetPlatformControls* ServerTP = new TGenericMacTargetPlatformControls<false, true, false>(OutMap[FMacPlatformProperties<false, true, false>::PlatformName()]);
			ITargetPlatformControls* ClientTP = new TGenericMacTargetPlatformControls<false, false, true>(OutMap[FMacPlatformProperties<false, false, true>::PlatformName()]);
			TargetPlatforms.Add(GameTP);
			TargetPlatforms.Add(EditorTP);
			TargetPlatforms.Add(ServerTP);
			TargetPlatforms.Add(ClientTP);
		}
	}

public:

	// Begin IModuleInterface interface

	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}

	// End IModuleInterface interface

};


#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE( FMacTargetPlatformControlsModule, MacTargetPlatformControls);
