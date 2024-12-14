// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextPool.h"
#include "Component/AnimNextWorldSubsystem.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleTaskContext.h"
#include "AnimNextComponentWorldSubsystem.generated.h"

class UAnimNextComponent;

// Represents AnimNext systems to the AActor/UActorComponent gameplay framework
UCLASS()
class UAnimNextComponentWorldSubsystem : public UAnimNextWorldSubsystem
{
	GENERATED_BODY()

	friend class UAnimNextComponent;

	// Register a component to the subsystem
	void Register(UAnimNextComponent* InComponent);

	// Unregister a component to the subsystem
	// The full release of the module referenced by the component's handle will be deferred after this call is made
	void Unregister(UAnimNextComponent* InComponent);

	// Enables or disables the module represented by the supplied handle
	// This operation is deferred until the next time the schedule ticks
	void SetEnabled(UAnimNextComponent* InComponent, bool bInEnabled);

	// Queue a task to run at a particular point in a schedule
	// @param	InComponent			The component to execute the task on
	// @param	InModuleEventName	The name of the event in the module to run the supplied task relative to. If this is NAME_None, then the first valid event will be used.
	// @param	InTaskFunction		The function to run
	// @param	InLocation			Where to run the task, before or after
	void QueueTask(UAnimNextComponent* InComponent, FName InModuleEventName, TUniqueFunction<void(const UE::AnimNext::FModuleTaskContext&)>&& InTaskFunction, UE::AnimNext::ETaskRunLocation InLocation = UE::AnimNext::ETaskRunLocation::Before);

#if WITH_EDITOR
	// UAnimNextWorldSubsystem interface
	virtual void OnModuleCompiled(UAnimNextModule* InModule) override;
#endif
};