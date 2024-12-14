// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextModule.h"
#include "ModuleHandle.h"
#include "Component/AnimNextPublicVariablesProxy.h"
#include "DataInterface/AnimNextDataInterfaceInstance.h"
#include "Module/ModuleTickFunction.h"
#include "TraitCore/TraitEvent.h"
#include "TraitCore/TraitEventList.h"
#include "Module/ModuleHandle.h"
#include "AnimNextModuleInstance.generated.h"

struct FAnimNextModuleInstance;

namespace UE::AnimNext
{
	struct FProxyVariablesContext;
}

namespace UE::AnimNext
{

// Phase is used as a general ordering constraint on event execution
enum class EModuleEventPhase : uint8
{
	// Before any execution, e.g. for copying data from the game thread
	PreExecute,

	// Any initial setup work for new instances
	Initialize,

	// General execution, e.g. a prephysics event
	Execute,
};

}

namespace UE::AnimNext::Private
{
	// Pre-caches all possible module event info for events that have been predefined in code  
	extern void CacheAllModuleEvents();
}

// Root memory owner of a parameterized schedule 
USTRUCT()
struct FAnimNextModuleInstance : public FAnimNextDataInterfaceInstance
{
	GENERATED_BODY()

	FAnimNextModuleInstance() = default;
	FAnimNextModuleInstance(
		UAnimNextModule* InModule,
		UObject* InObject,
		EAnimNextModuleInitMethod InInitMethod);
	~FAnimNextModuleInstance();

	// Setup the entry
	void Initialize();

	// Clear data that binds the schedule to a runtime (e.g. tick functions) and any instance data
	void ResetBindingsAndInstanceData();

	// Used for pooling
	void Invalidate();

	// Enables/disables the ticking of this entry
	void Enable(bool bInEnabled);

	// Clears the bTickEvenWhenPaused flags of the entries' tick functions
	void ClearTickFunctionPauseFlags();

	// Queues an input trait event
	// Input events will be processed in the next graph update after they are queued
	void QueueInputTraitEvent(FAnimNextTraitEventPtr Event);

	// Flip proxy variable buffers then copy any dirty values
	void CopyProxyVariables();

#if WITH_EDITOR
	// Resets internal state if the module we are bound to is recompiled in editor
	void OnModuleCompiled();
#endif

	// Get the module that this instance represents
	ANIMNEXT_API const UAnimNextModule* GetModule() const;

	// Object this entry is bound to
	UPROPERTY(Transient)
	TObjectPtr<UObject> Object = nullptr;

	// Copy of the handle that represents this entry to client systems
	UE::AnimNext::FModuleHandle Handle;

	// End tick function used to perform internal bookkeeping
	UE::AnimNext::FModuleEndTickFunction EndTickFunction;

	// Pre-allocated graph of tick functions
	TArray<UE::AnimNext::FModuleEventTickFunction> TickFunctions;

	// Input event list to be processed on the next update
	UE::AnimNext::FTraitEventList InputEventList;

	// Output event list to be processed at the end of the schedule tick
	UE::AnimNext::FTraitEventList OutputEventList;

	// Lock to ensure event list actions are thread safe
	FRWLock EventListLock;

	// Proxy public variables
	UPROPERTY(Transient)
	FAnimNextPublicVariablesProxy PublicVariablesProxy;

	enum class ERunState : uint8
	{
		None,

		CreatingTasks,

		BindingTasks,

		PendingInitialUpdate,

		Running,

		Paused,
	};

	// Current running state
	ERunState RunState = ERunState::None;

	// Transition to the specified run state, verifying that the current state is valid
	void TransitionToRunState(ERunState InNewState);
	
	// How this entry initializes
	EAnimNextModuleInitMethod InitMethod = EAnimNextModuleInitMethod::InitializeAndPauseInEditor;

	// Whether this represents an editor object 
	bool bIsEditor : 1 = false;

#if WITH_EDITOR
	// Whether we are currently recreating this instance because of compilation/reinstancing
	bool bIsRecreatingOnCompile : 1 = false;
#endif
};

template<>
struct TStructOpsTypeTraits<FAnimNextModuleInstance> : public TStructOpsTypeTraitsBase2<FAnimNextModuleInstance>
{
	enum
	{
		WithCopy = false
	};
};
