// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Logging/LogMacros.h"

HOLDOUTCOMPOSITE_API DECLARE_LOG_CATEGORY_EXTERN(LogHoldoutComposite, Log, All);

class FHoldoutCompositeModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End of IModuleInterface interface
};

