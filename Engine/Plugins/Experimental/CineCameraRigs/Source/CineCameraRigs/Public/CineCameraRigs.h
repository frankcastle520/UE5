// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FCineCameraRigsModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static float GetAbsolutePositionOnRail(const UObject* Object);
	static void SetAbsolutePositionOnRail(UObject* Object, float InNewValue);
};
