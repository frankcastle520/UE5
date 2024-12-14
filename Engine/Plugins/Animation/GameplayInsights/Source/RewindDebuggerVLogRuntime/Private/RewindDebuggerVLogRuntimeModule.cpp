// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerVLogRuntimeModule.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"

void FRewindDebuggerVLogRuntimeModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &VLogExtension);
}

void FRewindDebuggerVLogRuntimeModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &VLogExtension);
}

IMPLEMENT_MODULE(FRewindDebuggerVLogRuntimeModule, RewindDebuggerVLogRuntime);
