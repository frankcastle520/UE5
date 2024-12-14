// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Containers/SpscQueue.h"

struct FAnimNextModuleInstance;

namespace UE::AnimNext
{
	struct FScheduleContext;
	struct FModuleTaskContext;
}

namespace UE::AnimNext
{

struct FModuleEventTickFunction : public FTickFunction
{
	FModuleEventTickFunction()
	{
		ModuleInstance = nullptr;
		EventName = NAME_None;
		bCanEverTick = true;
		bStartWithTickEnabled = true;
		bRunOnAnyThread = true;
	}

	// FTickFunction interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;

	void Run(float DeltaTime);

#if WITH_EDITOR
	// Called standalone to run a whole module's init & work in one call. Expensive and editor only.
	// Intended to be used outside of a ticking context, such as a non-ticking world or forced initialization.
	static void InitializeAndRunModule(FAnimNextModuleInstance& InModuleInstance);
#endif

	FAnimNextModuleInstance* ModuleInstance = nullptr;
	FName EventName;
	TSpscQueue<TUniqueFunction<void(const FModuleTaskContext&)>> PreExecuteTasks;
	TSpscQueue<TUniqueFunction<void(const FModuleTaskContext&)>> PostExecuteTasks;
};

struct FModuleEndTickFunction : public FTickFunction
{
	FModuleEndTickFunction()
	{
		ModuleInstance = nullptr;
		bCanEverTick = true;
		bStartWithTickEnabled = true;
		bRunOnAnyThread = true;
	}

	// FTickFunction interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;

	void Run();

	FAnimNextModuleInstance* ModuleInstance = nullptr;
};


}