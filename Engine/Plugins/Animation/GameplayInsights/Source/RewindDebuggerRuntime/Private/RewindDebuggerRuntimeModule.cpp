// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerRuntimeModule.h"

#include "RewindDebuggerRuntime/RewindDebuggerRuntime.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "ObjectTrace.h"
#include "Features/IModularFeatures.h"

void FRewindDebuggerRuntimeModule::StartupModule()
{
	if (RewindDebugger::FRewindDebuggerRuntime::Instance() == nullptr)
	{
		RewindDebugger::FRewindDebuggerRuntime::Initialize();
	}
	
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("RewindDebugger.StartRecording"),
		TEXT("Starts making a rewind debugger recording."),
		FConsoleCommandWithArgsDelegate::CreateRaw(RewindDebugger::FRewindDebuggerRuntime::Instance(), &RewindDebugger::FRewindDebuggerRuntime::StartRecordingWithArgs),
		ECVF_Default
	));
	
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
   		TEXT("RewindDebugger.StopRecording"),
   		TEXT("Stops the current rewind debugger recording."),
		FConsoleCommandDelegate::CreateRaw(RewindDebugger::FRewindDebuggerRuntime::Instance(), &RewindDebugger::FRewindDebuggerRuntime::StopRecording),
   		ECVF_Default
   	));
	
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &AnimationExtension);
}

void FRewindDebuggerRuntimeModule::ShutdownModule()
{
	IConsoleManager &ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject *ConsoleObject : ConsoleObjects)
	{
		if (ConsoleObject)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleObject);
		}
	}
	ConsoleObjects.Empty();
	
	RewindDebugger::FRewindDebuggerRuntime::Shutdown();
	
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &AnimationExtension);
}

IMPLEMENT_MODULE(FRewindDebuggerRuntimeModule, RewindDebuggerRuntime);
