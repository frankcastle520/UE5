// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "RewindDebuggerAnimationRuntime.h"

class FRewindDebuggerRuntimeModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
private:
	TArray<IConsoleObject*> ConsoleObjects;

	RewindDebugger::FRewindDebuggerAnimationRuntime AnimationExtension;
};
