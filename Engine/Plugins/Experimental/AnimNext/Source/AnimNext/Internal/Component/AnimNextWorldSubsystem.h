// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextPool.h"
#include "Subsystems/WorldSubsystem.h"
#include "Module/ModuleHandle.h"
#include "Module/ModuleTickFunction.h"
#include "Module/AnimNextModuleInstance.h"
#include "Module/ModuleTaskContext.h"
#include "AnimNextWorldSubsystem.generated.h"

class UAnimNextComponent;

namespace UE::AnimNext
{

// A queued action to complete next frame
struct FModulePendingAction
{
	enum class EType : int32
	{
		None = 0,
		ReleaseHandle,
		EnableHandle,
		DisableHandle,
	};

	FModulePendingAction() = default;

	FModulePendingAction(EType InType, FModuleHandle InHandle);

	FModuleHandle Handle;

	EType Type = EType::None;
};

}

// Represents AnimNext systems to the gameplay framework
UCLASS(Abstract)
class ANIMNEXT_API UAnimNextWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UAnimNextWorldSubsystem();
	virtual ~UAnimNextWorldSubsystem() override;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	// UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	bool IsValidHandle(UE::AnimNext::FModuleHandle InHandle) const;

	void FlushPendingActions();

protected:
#if WITH_EDITOR
	// Refresh any entries that use the provided module as it has been recompiled.
	virtual void OnModuleCompiled(UAnimNextModule* InModule);
#endif
	
	// Register a handle to the subsystem
	void RegisterHandle(UE::AnimNext::FModuleHandle& InOutHandle, UAnimNextModule* InModule, UObject* InObject, EAnimNextModuleInitMethod InitMethod);

	// Unregister a handle from the subsystem
	void UnregisterHandle(UE::AnimNext::FModuleHandle& InOutHandle);

	// Enables or disables the module represented by the supplied handle
	// This operation is deferred until the next time the schedule ticks
	void EnableHandle(UE::AnimNext::FModuleHandle InHandle, bool bInEnabled);

	// Queue a task to run at a particular point in a schedule
	// @param	InHandle			The handle to execute the task on
	// @param	InModuleEventName	The name of the event in the module to run the supplied task relative to. If this is NAME_None, then the first valid event will be used.
	// @param	InTaskFunction		The function to run
	// @param	InLocation			Where to run the task, before or after
	void QueueTaskHandle(UE::AnimNext::FModuleHandle InHandle, FName InModuleEventName, TUniqueFunction<void(const UE::AnimNext::FModuleTaskContext&)>&& InTaskFunction, UE::AnimNext::ETaskRunLocation InLocation = UE::AnimNext::ETaskRunLocation::Before);

protected:
	// Currently running instances, pooled
	UE::AnimNext::TPool<FAnimNextModuleInstance> Instances;

	// Queued actions
	TArray<UE::AnimNext::FModulePendingAction> PendingActions;

	// Locks for concurrent modifications
	FRWLock InstancesLock;
	FRWLock PendingLock;

	// Handle used to hook into pre-world tick
	FDelegateHandle OnWorldPreActorTickHandle;

	// Cached delta time
	float DeltaTime;
};