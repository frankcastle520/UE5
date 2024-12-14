// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "RewindDebuggerVLogRuntime.h"

class FRewindDebuggerVLogRuntimeModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	RewindDebugger::FRewindDebuggerVLogRuntime VLogExtension;
};
