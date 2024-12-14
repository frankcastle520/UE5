// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestSubsystem.h"

#include "AutomatedPerfTestControllerBase.h"
#include "GauntletModule.h"
#include "Modules/ModuleManager.h"

FString UAutomatedPerfTestSubsystem::GetTestID()
{
	if(FGauntletModule* ParentModule = &FModuleManager::Get().GetModuleChecked<FGauntletModule>(TEXT("Gauntlet")))
	{
		if(UAutomatedPerfTestControllerBase* Controller = ParentModule->GetTestController<UAutomatedPerfTestControllerBase>())
		{
			return Controller->GetTestID();
		}
	}
	return TEXT("");
}
