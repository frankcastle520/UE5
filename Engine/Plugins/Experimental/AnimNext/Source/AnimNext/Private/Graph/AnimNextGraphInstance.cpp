// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextGraphInstance.h"

#include "AnimNextStats.h"
#include "DataInterface/AnimNextDataInterfaceHost.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "TraitCore/ExecutionContext.h"
#include "Graph/GC_GraphInstanceComponent.h"
#include "Graph/RigUnit_AnimNextShimRoot.h"
#include "Graph/RigVMTrait_AnimNextPublicVariables.h"
#include "Logging/StructuredLog.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModuleInstance.h"

DEFINE_STAT(STAT_AnimNext_Graph_RigVM);

FAnimNextGraphInstance::FAnimNextGraphInstance()
{
#if WITH_EDITORONLY_DATA
	UAnimNextModule::OnModuleCompiled().AddRaw(this, &FAnimNextGraphInstance::OnModuleCompiled);
#endif
}

FAnimNextGraphInstance::~FAnimNextGraphInstance()
{
	Release();
}

void FAnimNextGraphInstance::Release()
{
#if WITH_EDITORONLY_DATA
	UAnimNextModule::OnModuleCompiled().RemoveAll(this);

	if(const UAnimNextAnimationGraph* Graph = GetAnimationGraph())
	{
		FScopeLock Lock(&Graph->GraphInstancesLock);
		Graph->GraphInstances.Remove(this);
	}
#endif

	if (!GraphInstancePtr.IsValid())
	{
		return;
	}

	GraphInstancePtr.Reset();
	ModuleInstance = nullptr;
	ParentGraphInstance = nullptr;
	RootGraphInstance = nullptr;
	ExtendedExecuteContext.Reset();
	Components.Empty();
	DataInterface = nullptr;
}

bool FAnimNextGraphInstance::IsValid() const
{
	return GraphInstancePtr.IsValid();
}

const UAnimNextAnimationGraph* FAnimNextGraphInstance::GetAnimationGraph() const
{
	return CastChecked<UAnimNextAnimationGraph>(DataInterface);
}

FName FAnimNextGraphInstance::GetEntryPoint() const
{
	return EntryPoint;
}

UE::AnimNext::FWeakTraitPtr FAnimNextGraphInstance::GetGraphRootPtr() const
{
	return GraphInstancePtr;
}

FAnimNextModuleInstance* FAnimNextGraphInstance::GetModuleInstance() const
{
	return ModuleInstance;
}

FAnimNextGraphInstance* FAnimNextGraphInstance::GetParentGraphInstance() const
{
	return ParentGraphInstance;
}

FAnimNextGraphInstance* FAnimNextGraphInstance::GetRootGraphInstance() const
{
	return RootGraphInstance;
}

bool FAnimNextGraphInstance::UsesAnimationGraph(const UAnimNextAnimationGraph* InAnimationGraph) const
{
	return GetAnimationGraph() == InAnimationGraph;
}

bool FAnimNextGraphInstance::UsesEntryPoint(FName InEntryPoint) const
{
	if(const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph())
	{
		if(InEntryPoint == NAME_None)
		{
			return EntryPoint == AnimationGraph->DefaultEntryPoint;
		}

		return InEntryPoint == EntryPoint;
	}
	return false;
}

bool FAnimNextGraphInstance::IsRoot() const
{
	return this == RootGraphInstance;
}

bool FAnimNextGraphInstance::HasUpdated() const
{
	return bHasUpdatedOnce;
}

void FAnimNextGraphInstance::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	if (!IsRoot())
	{
		return;	// If we aren't the root graph instance, we don't own the components
	}

	if (const UE::AnimNext::FGCGraphInstanceComponent* Component = TryGetComponent<UE::AnimNext::FGCGraphInstanceComponent>())
	{
		Component->AddReferencedObjects(Collector);
	}
}

UE::AnimNext::FGraphInstanceComponent* FAnimNextGraphInstance::TryGetComponent(int32 ComponentNameHash, FName ComponentName) const
{
	if (const TSharedPtr<UE::AnimNext::FGraphInstanceComponent>* Component = RootGraphInstance->Components.FindByHash(ComponentNameHash, ComponentName))
	{
		return Component->Get();
	}

	return nullptr;
}

UE::AnimNext::FGraphInstanceComponent& FAnimNextGraphInstance::AddComponent(int32 ComponentNameHash, FName ComponentName, TSharedPtr<UE::AnimNext::FGraphInstanceComponent>&& Component)
{
	return *RootGraphInstance->Components.AddByHash(ComponentNameHash, ComponentName, MoveTemp(Component)).Get();
}

GraphInstanceComponentMapType::TConstIterator FAnimNextGraphInstance::GetComponentIterator() const
{
	return RootGraphInstance->Components.CreateConstIterator();
}

void FAnimNextGraphInstance::Update()
{
	bHasUpdatedOnce = true;
}

FAnimNextDataInterfaceInstance* FAnimNextGraphInstance::GetHost() const
{
	if(ParentGraphInstance != nullptr)
	{
		return ParentGraphInstance;
	}

	return ModuleInstance;
}

void FAnimNextGraphInstance::ExecuteLatentPins(const TConstArrayView<UE::AnimNext::FLatentPropertyHandle>& LatentHandles, void* DestinationBasePtr, bool bIsFrozen)
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Graph_RigVM);

	if (!IsValid())
	{
		return;
	}

	if (URigVM* VM = GetAnimationGraph()->RigVM)
	{
		FAnimNextExecuteContext& AnimNextContext = ExtendedExecuteContext.GetPublicDataSafe<FAnimNextExecuteContext>();
		AnimNextContext.SetContextData<FAnimNextGraphContextData>(ModuleInstance, this, LatentHandles, DestinationBasePtr, bIsFrozen);

		VM->ExecuteVM(ExtendedExecuteContext, FRigUnit_AnimNextShimRoot::EventName);

		// Reset the context to avoid issues if we forget to reset it the next time we use it
		AnimNextContext.DebugReset<FAnimNextGraphContextData>();
	}
}

#if WITH_EDITORONLY_DATA
void FAnimNextGraphInstance::Freeze()
{
	if (!IsValid())
	{
		return;
	}

	GraphInstancePtr.Reset();
	ExtendedExecuteContext.Reset();
	Components.Empty();
	PublicVariablesState = PublicVariablesState == EPublicVariablesState::Bound ? EPublicVariablesState::Unbound : EPublicVariablesState::None;
	bHasUpdatedOnce = false;
}

void FAnimNextGraphInstance::Thaw()
{
	if (const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph())
	{
		Variables.MigrateToNewBagInstance(AnimationGraph->VariableDefaults);

		ExtendedExecuteContext = AnimationGraph->ExtendedExecuteContext;

		{
			UE::AnimNext::FExecutionContext Context(*this);
			if(const FAnimNextTraitHandle* FoundHandle = AnimationGraph->ResolvedRootTraitHandles.Find(EntryPoint))
			{
				GraphInstancePtr = Context.AllocateNodeInstance(*this, *FoundHandle);
			}
		}

		if (!IsValid())
		{
			// We failed to allocate our instance, clear everything
			Release();
		}
	}
}

void FAnimNextGraphInstance::OnModuleCompiled(UAnimNextModule* InModule)
{
	// If we are hosted directly by a module, invalidate and mark our bindings as needing update. They will be lazily re-bound the next time we run.
	if(ModuleInstance && InModule == ModuleInstance->GetModule() && ParentGraphInstance == nullptr)
	{
		UnbindPublicVariables();
	}
}

#endif

// Helper for binding to both FAnimNextDataInterfaceInstance and IDataInterfaceHost without an abstraction between the two
template<typename HostType>
bool FAnimNextGraphInstance::BindToHostHelper(const HostType& InHost, bool bInAutoBind)
{
	bool bPublicVariablesBound = false;

	const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph();
	const UPropertyBag* PropertyBag = Variables.GetPropertyBagStruct();
	for(const FAnimNextImplementedDataInterface& ImplementedInterface : AnimationGraph->GetImplementedInterfaces())
	{
		if(bInAutoBind && !ImplementedInterface.bAutoBindToHost)
		{
			continue;
		}

		const FAnimNextImplementedDataInterface* HostImplementedInterface = InHost.GetDataInterface()->FindImplementedInterface(ImplementedInterface.DataInterface);
		if(HostImplementedInterface == nullptr)
		{
			// Host does not implement this interface, so skip
			continue;
		}

		if(HostImplementedInterface->NumVariables != ImplementedInterface.NumVariables)
		{
			UE_LOGFMT(LogAnimation, Error, "BindToHost: Mismatched interface variables: '{Name}' ({Count}) vs Host '{Host}' ({HostCount})", AnimationGraph->GetFName(), ImplementedInterface.NumVariables, InHost.GetDataInterfaceName(), HostImplementedInterface->NumVariables);
			continue;
		}

		int32 VariableIndex = ImplementedInterface.VariableIndex;
		int32 HostVariableIndex = HostImplementedInterface->VariableIndex;
		const int32 EndVariableIndex = ImplementedInterface.VariableIndex + ImplementedInterface.NumVariables;
		for(; VariableIndex < EndVariableIndex; ++VariableIndex, ++HostVariableIndex)
		{
			const FPropertyBagPropertyDesc& Desc = PropertyBag->GetPropertyDescs()[VariableIndex];

			uint8* HostMemory = InHost.GetMemoryForVariable(HostVariableIndex, Desc.Name, Desc.CachedProperty);
			if(HostMemory == nullptr)
			{
				continue;
			}

			ExtendedExecuteContext.ExternalVariableRuntimeData[VariableIndex].Memory = HostMemory;
			bPublicVariablesBound = true;
		}
	}

	return bPublicVariablesBound;
}

void FAnimNextGraphInstance::BindPublicVariables(TConstArrayView<UE::AnimNext::IDataInterfaceHost*> InHosts)
{
	using namespace UE::AnimNext;

	const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph();
	if (AnimationGraph == nullptr)
	{
		return;
	}

	if(PublicVariablesState == EPublicVariablesState::Bound)
	{
		return;
	}

	const UPropertyBag* PropertyBag = Variables.GetPropertyBagStruct();
	if(PropertyBag == nullptr)
	{
		// Nothing to bind
		PublicVariablesState = EPublicVariablesState::None;
		return;
	}

	bool bPublicVariablesBound = false;

	// First apply any automatic bindings to this instance's host
	if(FAnimNextDataInterfaceInstance* InstanceHost = GetHost())
	{
		bPublicVariablesBound |= BindToHostHelper(*InstanceHost, true);
	}

	// Next bind to any supplied host interfaces (bInAutoBind = false)
	for(const IDataInterfaceHost* HostInterface : InHosts)
	{
		check(HostInterface);
		bPublicVariablesBound |= BindToHostHelper(*HostInterface, false);
	}

	if(bPublicVariablesBound)
	{
		// Re-initialize memory handles
		AnimationGraph->RigVM->InitializeInstance(ExtendedExecuteContext, /* bCopyMemory = */false);
	}

	PublicVariablesState = EPublicVariablesState::Bound;
}

void FAnimNextGraphInstance::UnbindPublicVariables()
{
	const UAnimNextAnimationGraph* AnimationGraph = GetAnimationGraph();
	if (AnimationGraph == nullptr)
	{
		return;
	}

	if(PublicVariablesState != EPublicVariablesState::Bound)
	{
		return;
	}

	// Reset external variable ptrs to point to internal public vars
	const int32 NumVariables = Variables.GetNumPropertiesInBag();
	TConstArrayView<FPropertyBagPropertyDesc> Descs = Variables.GetPropertyBagStruct()->GetPropertyDescs();
	uint8* BasePtr = Variables.GetMutableValue().GetMemory();
	for(int32 VariableIndex = 0; VariableIndex < NumVariables; ++VariableIndex)
	{
		ExtendedExecuteContext.ExternalVariableRuntimeData[VariableIndex].Memory = Descs[VariableIndex].CachedProperty->ContainerPtrToValuePtr<uint8>(BasePtr);
	}

	// Re-initialize memory handles
	AnimationGraph->RigVM->InitializeInstance(ExtendedExecuteContext, /* bCopyMemory = */false);

	PublicVariablesState = EPublicVariablesState::Unbound;
}

