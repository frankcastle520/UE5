// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextModuleInstance.h"

#include "Engine/World.h"
#include "AnimNextStats.h"
#include "Algo/TopologicalSort.h"
#include "Logging/StructuredLog.h"
#include "Misc/EnumerateRange.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"
#include "Module/ProxyVariablesContext.h"
#include "UObject/UObjectIterator.h"
#include "Variables/IAnimNextVariableProxyHost.h"

DEFINE_STAT(STAT_AnimNext_InitializeInstance);

FAnimNextModuleInstance::FAnimNextModuleInstance(
		UAnimNextModule* InModule,
		UObject* InObject,
		EAnimNextModuleInitMethod InInitMethod)
	: Object(InObject)
	, RunState(ERunState::None)
	, InitMethod(InInitMethod)
{
	DataInterface = InModule;
}

FAnimNextModuleInstance::~FAnimNextModuleInstance()
{
	ResetBindingsAndInstanceData();

	Object = nullptr;
	DataInterface = nullptr;
	Handle.Reset();
}

namespace UE::AnimNext::Private
{

struct FImplementedModuleEvent
{
	UScriptStruct* Struct = nullptr;
	FModuleEventBindingFunction Binding;
	FName EventName;
	EModuleEventPhase Phase = EModuleEventPhase::Execute;
};

static TArray<FImplementedModuleEvent> GAllModuleEvents;
static TArray<FImplementedModuleEvent> GImplementedModuleEvents;

void CacheAllModuleEvents()
{
	check(IsInGameThread());	// This function cannot be run concurrently because of static usage

	if(GAllModuleEvents.Num() == 0)
	{
		for(TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			if(Struct->IsChildOf(FRigUnit_AnimNextModuleEventBase::StaticStruct()) && Struct != FRigUnit_AnimNextModuleEventBase::StaticStruct())
			{
				TInstancedStruct<FRigUnit_AnimNextModuleEventBase> StructInstance;
				StructInstance.InitializeAsScriptStruct(Struct);
				FImplementedModuleEvent& NewEvent = GAllModuleEvents.AddDefaulted_GetRef();
				NewEvent.Struct = Struct;
				NewEvent.Binding = StructInstance.Get().GetBindingFunction();
				NewEvent.EventName = StructInstance.Get().GetEventName();
				NewEvent.Phase = StructInstance.Get().GetEventPhase();
			}
		}

		Algo::SortBy(GAllModuleEvents, &FImplementedModuleEvent::Phase);
		GImplementedModuleEvents.Reserve(GAllModuleEvents.Num());
	}
}

// Gets information about the module events that are implemented by the supplied VM
static TConstArrayView<FImplementedModuleEvent> GetImplementedModuleEvents(const URigVM* VM)
{
	check(IsInGameThread());	// This function cannot be run concurrently because of static usage
	check(GAllModuleEvents.Num() > 0);	// Call CacheAllModuleEvents before this function

	// Get all the module events from the VM entry points
	const TArray<FName>& EntryNames = VM->GetEntryNames();
	GImplementedModuleEvents.Reset();
	for(const FImplementedModuleEvent& ModuleEvent : GAllModuleEvents)
	{
		if(EntryNames.Contains(ModuleEvent.EventName))
		{
			GImplementedModuleEvents.Add(ModuleEvent);
		}
	}

	return GImplementedModuleEvents;
}

}

void FAnimNextModuleInstance::Initialize()
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_InitializeInstance);
	
	using namespace UE::AnimNext;

	check(IsInGameThread());

	check(Object)
	const UAnimNextModule* Module = GetModule();
	check(Module);
	check(Handle.IsValid());

	UWorld* World = Object->GetWorld();
	bIsEditor = World && World->WorldType == EWorldType::Editor;

	// Get all the module events from the VM entry points, sorted by phase
	URigVM* VM = Module->RigVM;
	TConstArrayView<Private::FImplementedModuleEvent> ImplementedModuleEvents = Private::GetImplementedModuleEvents(VM);

	// Setup tick function graph using module events
	if (ImplementedModuleEvents.Num() > 0)
	{
		TransitionToRunState(ERunState::CreatingTasks);

		// Allocate tick functions
		TickFunctions.SetNum(ImplementedModuleEvents.Num()); 
		for (TEnumerateRef<const Private::FImplementedModuleEvent> ModuleEvent : EnumerateRange(ImplementedModuleEvents))
		{
			FModuleEventTickFunction& TickFunction = TickFunctions[ModuleEvent.GetIndex()];
			TickFunction.ModuleInstance = this;
			TickFunction.EventName = ModuleEvent->EventName;
		}

		// Bind tick functions (done in a second pass to allow functions to add prerequisites between each other)
		for (TEnumerateRef<const Private::FImplementedModuleEvent> ModuleEvent : EnumerateRange(ImplementedModuleEvents))
		{
			FModuleEventTickFunction& TickFunction = TickFunctions[ModuleEvent.GetIndex()];
			// Add prerequisite on end tick function
			EndTickFunction.AddPrerequisite(Object, TickFunction);
			FTickFunctionBindingContext Context(*this, Object, World, ModuleEvent.GetIndex());
			ModuleEvent->Binding(Context, TickFunction);
		}

		// Bind end tick function to this instance
		EndTickFunction.ModuleInstance = this;

		TransitionToRunState(ERunState::BindingTasks);

		// Register our tick functions
		if(World)
		{
			ULevel* Level = World->PersistentLevel;
			for (FModuleEventTickFunction& TickFunction : TickFunctions)
			{
				TickFunction.RegisterTickFunction(Level);
			}
			EndTickFunction.RegisterTickFunction(Level);
		}

		TransitionToRunState(ERunState::PendingInitialUpdate);

		// Initialize variables
		const int32 NumVariables = Module->VariableDefaults.GetNumPropertiesInBag();
#if WITH_EDITOR
		if(bIsRecreatingOnCompile)
		{
			Variables.MigrateToNewBagInstance(Module->VariableDefaults);
		}
		else
#endif
		{
			Variables = Module->VariableDefaults;
		}

		if(Module->GetPublicVariableDefaults().GetPropertyBagStruct())
		{
			PublicVariablesProxy.Data = Module->GetPublicVariableDefaults();
			TConstArrayView<FPropertyBagPropertyDesc> ProxyDescs = PublicVariablesProxy.Data.GetPropertyBagStruct()->GetPropertyDescs();
			PublicVariablesProxy.DirtyFlags.SetNum(ProxyDescs.Num(), false);
		}

		// Initialize the RigVM context
		ExtendedExecuteContext = Module->GetRigVMExtendedExecuteContext();

		// Setup external variables memory ptrs manually as we dont follow the pattern of owning multiple URigVMHosts like control rig.
		// InitializeVM() is called, but only sets up handles for the defaults in the module, not for an instance
		TArray<FRigVMExternalVariableRuntimeData> ExternalVariableRuntimeData;
		ExternalVariableRuntimeData.Reserve(NumVariables);
		TConstArrayView<FPropertyBagPropertyDesc> Descs = Variables.GetPropertyBagStruct()->GetPropertyDescs();
		uint8* BasePtr = Variables.GetMutableValue().GetMemory();
		for(int32 VariableIndex = 0; VariableIndex < NumVariables; ++VariableIndex)
		{
			ExternalVariableRuntimeData.Emplace(Descs[VariableIndex].CachedProperty->ContainerPtrToValuePtr<uint8>(BasePtr));
		}
		ExtendedExecuteContext.ExternalVariableRuntimeData = MoveTemp(ExternalVariableRuntimeData);

		// Now initialize the 'instance', cache memory handles etc. in the context
		VM->InitializeInstance(ExtendedExecuteContext);

		// Just pause now if we arent needing an initial update
		if(InitMethod == EAnimNextModuleInitMethod::None)
		{
			Enable(false);
		}
#if WITH_EDITOR
		else if(World)
		{
			// In editor worlds we run a linearized 'initial tick' to ensure we generate an initial output pose, as these worlds dont always tick
			if( World->WorldType == EWorldType::Editor ||
				World->WorldType == EWorldType::EditorPreview)
			{
				FModuleEventTickFunction::InitializeAndRunModule(*this);
			}
		}
#endif
	}
}

void FAnimNextModuleInstance::ResetBindingsAndInstanceData()
{
	using namespace UE::AnimNext;

	check(IsInGameThread());

	TransitionToRunState(ERunState::None);

	for (FModuleEventTickFunction& TickFunction : TickFunctions)
	{
		TickFunction.UnRegisterTickFunction();
	}

	EndTickFunction.UnRegisterTickFunction();

	TickFunctions.Reset();

	ExtendedExecuteContext.Reset();

#if WITH_EDITOR
	if(!bIsRecreatingOnCompile)
#endif
	{
		Variables.Reset();
	}
}

void FAnimNextModuleInstance::Invalidate()
{
	using namespace UE::AnimNext;

	ResetBindingsAndInstanceData();

	DataInterface = nullptr;
	Object = nullptr;
	Handle.Reset();
}

void FAnimNextModuleInstance::ClearTickFunctionPauseFlags()
{
	using namespace UE::AnimNext;

	check(IsInGameThread());

	for (FModuleEventTickFunction& TickFunction : TickFunctions)
	{
		TickFunction.bTickEvenWhenPaused = false;
	}
	EndTickFunction.bTickEvenWhenPaused = false;
}

void FAnimNextModuleInstance::QueueInputTraitEvent(FAnimNextTraitEventPtr Event)
{
	InputEventList.Push(MoveTemp(Event));
}

void FAnimNextModuleInstance::Enable(bool bInEnabled)
{
	using namespace UE::AnimNext;

	check(IsInGameThread());

	if(RunState == ERunState::PendingInitialUpdate || RunState == ERunState::Paused || RunState == ERunState::Running)
	{
		for (FModuleEventTickFunction& TickFunction : TickFunctions)
		{
			TickFunction.SetTickFunctionEnable(bInEnabled);
		}
		EndTickFunction.SetTickFunctionEnable(bInEnabled);

		TransitionToRunState(bInEnabled ? ERunState::Running : ERunState::Paused);
	}
}

void FAnimNextModuleInstance::TransitionToRunState(ERunState InNewState)
{
	switch(InNewState)
	{
	case ERunState::None:
		check(RunState == ERunState::None || RunState == ERunState::PendingInitialUpdate || RunState == ERunState::Paused || RunState == ERunState::Running);
		break;
	case ERunState::CreatingTasks:
		check(RunState == ERunState::None);
		break;
	case ERunState::BindingTasks:
		check(RunState == ERunState::CreatingTasks);
		break;
	case ERunState::PendingInitialUpdate:
		check(RunState == ERunState::BindingTasks);
		break;
	case ERunState::Running:
		check(RunState == ERunState::PendingInitialUpdate || RunState == ERunState::Paused || RunState == ERunState::Running);
		break;
	case ERunState::Paused:
		check(RunState == ERunState::PendingInitialUpdate || RunState == ERunState::Paused || RunState == ERunState::Running);
		break;
	default:
		checkNoEntry();
	}

	RunState = InNewState;
}

void FAnimNextModuleInstance::CopyProxyVariables()
{
	// TODO: we can avoid the copies here by adopting a scheme where we:
	// - Hold double-buffered memory handles
	// - Update the memory handle's ptr to the currently-written double-buffered public variable on write
	// - Swap the memory handles in ExtendedExecuteContext here
	if(IAnimNextVariableProxyHost* ProxyHost = Cast<IAnimNextVariableProxyHost>(Object))
	{
		// Flip the proxy
		ProxyHost->FlipPublicVariablesProxy(UE::AnimNext::FProxyVariablesContext(*this));

		if(PublicVariablesProxy.bIsDirty)
		{
			// Copy dirty properties
			TConstArrayView<FPropertyBagPropertyDesc> ProxyDescs = Variables.GetPropertyBagStruct()->GetPropertyDescs();
			TConstArrayView<FPropertyBagPropertyDesc> PublicProxyDescs = PublicVariablesProxy.Data.GetPropertyBagStruct()->GetPropertyDescs();
			const uint8* SourceContainerPtr = PublicVariablesProxy.Data.GetValue().GetMemory();
			uint8* TargetContainerPtr = Variables.GetMutableValue().GetMemory();
			for (TConstSetBitIterator<> It(PublicVariablesProxy.DirtyFlags); It; ++It)
			{
				const int32 Index = It.GetIndex();
				const FProperty* SourceProperty = PublicProxyDescs[Index].CachedProperty;
				const FProperty* TargetProperty = ProxyDescs[Index].CachedProperty;
				checkSlow(SourceProperty->GetClass() == TargetProperty->GetClass());
				ProxyDescs[Index].CachedProperty->CopyCompleteValue_InContainer(TargetContainerPtr, SourceContainerPtr);
			}

			// Reset dirty flags
			PublicVariablesProxy.DirtyFlags.SetRange(0, PublicVariablesProxy.DirtyFlags.Num(), false);
			PublicVariablesProxy.bIsDirty = false;
		}
	}
}

const UAnimNextModule* FAnimNextModuleInstance::GetModule() const
{
	return CastChecked<UAnimNextModule>(DataInterface);
}

#if WITH_EDITOR
void FAnimNextModuleInstance::OnModuleCompiled()
{
	using namespace UE::AnimNext;

	FGuardValue_Bitfield(bIsRecreatingOnCompile, true);

	ResetBindingsAndInstanceData();
	Initialize();
}
#endif