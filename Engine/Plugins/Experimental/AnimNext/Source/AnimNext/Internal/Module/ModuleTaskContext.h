// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TraitCore/TraitEvent.h"

struct FAnimNextModuleInstance;
struct FInstancedPropertyBag;

namespace UE::AnimNext
{
	struct FScheduleContext;
	enum class EParameterScopeOrdering : int32;
	struct FModuleEventTickFunction;
}

namespace UE::AnimNext
{

enum class ETaskRunLocation : int32
{
	// Run the task before the specified task
	Before,

	// Run the task after the specified task
	After,
};

// Context passed to schedule task callbacks
struct FModuleTaskContext
{
public:
	// Queues an input trait event
	// Input events will be processed in the next graph update after they are queued
	ANIMNEXT_API void QueueInputTraitEvent(FAnimNextTraitEventPtr Event) const;

private:
	FModuleTaskContext(FAnimNextModuleInstance& InModuleInstance);

	// The module instance currently running
	FAnimNextModuleInstance* ModuleInstance;
	
	friend struct FModuleEventTickFunction;
};

}