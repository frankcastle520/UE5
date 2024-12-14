// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * The public interface to this module
 */
class IDataflowSimulationPlugin : public IModuleInterface
{
public:
	virtual void StartupModule();
	virtual void ShutdownModule();
};

