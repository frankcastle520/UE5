// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

/**
 * Implements the ColorManagement module.
 */
class FColorManagementModule : public IModuleInterface
{
public:

	//~ IModuleInterface interface
	virtual void StartupModule() override { }
	virtual void ShutdownModule() override { }
};


IMPLEMENT_MODULE(FColorManagementModule, ColorManagement);
