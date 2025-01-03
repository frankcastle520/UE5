// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(ColorCorrectRegionsEditorLogOutput, Log, All);

class FColorCorrectRegionsEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation start */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/** IModuleInterface implementation end */

	void RegisterSectionMappings();
	void OnPlacementModeRefresh(FName CategoryName);

private:
	TSharedPtr<class FColorCorrectionActorContextMenu> ContextMenu;
};
