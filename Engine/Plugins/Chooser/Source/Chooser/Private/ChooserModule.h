// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "RewindDebuggerChooserRuntime.h"

class FChooserModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	FRewindDebuggerChooserRuntime ChooserExtension;
};
