// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/AnimNextAnimationGraph.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "Graph/RigUnit_AnimNextShimRoot.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectSaveContext.h"
#include "TraitCore/TraitReader.h"
#include "TraitCore/ExecutionContext.h"
#include "Graph/AnimNext_LODPose.h"
#include "Graph/AnimNextGraphInstance.h"
#include "Serialization/MemoryReader.h"
#include "AnimNextStats.h"
#include "Graph/AnimNextGraphEntryPoint.h"
#include "UObject/LinkerLoad.h"
#include "UObject/ObjectResource.h"

#if WITH_EDITOR	
#include "Engine/ExternalAssetDependencyGatherer.h"
REGISTER_ASSETDEPENDENCY_GATHERER(FExternalAssetDependencyGatherer, UAnimNextAnimationGraph);
#endif // WITH_EDITOR

DEFINE_STAT(STAT_AnimNext_Graph_AllocateInstance);
DEFINE_STAT(STAT_AnimNext_Graph_UpdateParamLayer);

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextAnimationGraph)

UAnimNextAnimationGraph::UAnimNextAnimationGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ExtendedExecuteContext.SetContextPublicDataStruct(FAnimNextExecuteContext::StaticStruct());
}

void UAnimNextAnimationGraph::AllocateInstance(FAnimNextGraphInstancePtr& OutInstance, FAnimNextModuleInstance* InModuleInstance, FName InEntryPoint) const
{
	AllocateInstanceImpl(InModuleInstance, nullptr, OutInstance, InEntryPoint);
}

void UAnimNextAnimationGraph::AllocateInstance(FAnimNextGraphInstance& InParentGraphInstance, FAnimNextGraphInstancePtr& OutInstance, FName InEntryPoint) const
{
	AllocateInstanceImpl(InParentGraphInstance.GetModuleInstance(), &InParentGraphInstance, OutInstance, InEntryPoint);
}

void UAnimNextAnimationGraph::AllocateInstanceImpl(FAnimNextModuleInstance* InModuleInstance, FAnimNextGraphInstance* ParentGraphInstance, FAnimNextGraphInstancePtr& Instance, FName InEntryPoint) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Graph_AllocateInstance);

	Instance.Release();

	const FName EntryPoint = (InEntryPoint == NAME_None) ? DefaultEntryPoint : InEntryPoint;
	const FAnimNextTraitHandle ResolvedRootTraitHandle = ResolvedRootTraitHandles.FindRef(EntryPoint);
	if (!ResolvedRootTraitHandle.IsValid())
	{
		return;
	}

	{
		TSharedPtr<FAnimNextGraphInstance> InstanceImpl = MakeShared<FAnimNextGraphInstance>();

		InstanceImpl->DataInterface = this;
		InstanceImpl->ModuleInstance = InModuleInstance;
		InstanceImpl->ParentGraphInstance = ParentGraphInstance;
		InstanceImpl->EntryPoint = EntryPoint;

		// If we have a parent graph, use its root since we share the same root, otherwise if we have no parent, we are the root
		InstanceImpl->RootGraphInstance = ParentGraphInstance != nullptr ? ParentGraphInstance->GetRootGraphInstance() : InstanceImpl.Get();

		InstanceImpl->Variables = VariableDefaults;
		InstanceImpl->ExtendedExecuteContext = ExtendedExecuteContext;
		if(VariableDefaults.GetPropertyBagStruct() != nullptr)
		{
			InstanceImpl->PublicVariablesState = FAnimNextGraphInstance::EPublicVariablesState::Unbound;

			// Setup external variables memory ptrs manually as we dont follow the pattern of owning multiple URigVMHosts like control rig.
			// InitializeVM() is called, but only sets up handles for the defaults in the module, not for an instance
			const int32 NumVariables = InstanceImpl->Variables.GetNumPropertiesInBag();
			TArray<FRigVMExternalVariableRuntimeData> ExternalVariableRuntimeData;
			ExternalVariableRuntimeData.Reserve(NumVariables);
			TConstArrayView<FPropertyBagPropertyDesc> Descs = InstanceImpl->Variables.GetPropertyBagStruct()->GetPropertyDescs();
			uint8* BasePtr = InstanceImpl->Variables.GetMutableValue().GetMemory();
			for(int32 VariableIndex = 0; VariableIndex < NumVariables; ++VariableIndex)
			{
				ExternalVariableRuntimeData.Emplace(Descs[VariableIndex].CachedProperty->ContainerPtrToValuePtr<uint8>(BasePtr));
			}
			InstanceImpl->ExtendedExecuteContext.ExternalVariableRuntimeData = MoveTemp(ExternalVariableRuntimeData);
		}
		else
		{
			InstanceImpl->PublicVariablesState = FAnimNextGraphInstance::EPublicVariablesState::None;
		}

		// Now initialize the 'instance', cache memory handles etc. in the context
		VM->InitializeInstance(InstanceImpl->ExtendedExecuteContext);

		// Move our implementation so that we can use the instance to allocate our root node
		Instance.Impl = InstanceImpl;
	}

	{
		UE::AnimNext::FExecutionContext Context(Instance);
		Instance.Impl->GraphInstancePtr = Context.AllocateNodeInstance(*Instance.Impl, ResolvedRootTraitHandle);
	}

	if (!Instance.IsValid())
	{
		// We failed to allocate our instance, clear everything
		Instance.Release();
	}

#if WITH_EDITORONLY_DATA
	if (Instance.IsValid())
	{
		FScopeLock Lock(&GraphInstancesLock);
		check(!GraphInstances.Contains(Instance.Impl.Get()));
		GraphInstances.Add(Instance.Impl.Get());
	}
#endif
}

void UAnimNextAnimationGraph::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		if(Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextCombineParameterBlocksAndGraphs)
		{
			// Skip over shared archive buffer if we are loading from an older version
			if (const FLinkerLoad* Linker = GetLinker())
			{
				const int32 LinkerIndex = GetLinkerIndex();
				const FObjectExport& Export = Linker->ExportMap[LinkerIndex];
				Ar.Seek(Export.SerialOffset + Export.SerialSize);
			}
		}
		else
		{
			int32 SharedDataArchiveBufferSize = 0;
			Ar << SharedDataArchiveBufferSize;

#if !WITH_EDITORONLY_DATA
			// When editor data isn't present, we don't persist the archive buffer as it is only needed on load
			// to populate the graph shared data
			TArray<uint8> SharedDataArchiveBuffer;
#endif

			SharedDataArchiveBuffer.SetNumUninitialized(SharedDataArchiveBufferSize);
			Ar.Serialize(SharedDataArchiveBuffer.GetData(), SharedDataArchiveBufferSize);

			if (Ar.IsLoadingFromCookedPackage())
			{
				// If we are cooked, we populate our graph shared data otherwise in the editor we'll compile on load
				// and re-populate everything then to account for changes in code/content
				LoadFromArchiveBuffer(SharedDataArchiveBuffer);
			}
		}
	}
	else if (Ar.IsSaving())
	{
#if WITH_EDITORONLY_DATA
		// We only save the archive buffer, if code changes we'll be able to de-serialize from it when
		// building the runtime buffer
		// This allows us to have editor only/non-shipping only properties that are stripped out on load
		int32 SharedDataArchiveBufferSize = SharedDataArchiveBuffer.Num();
		Ar << SharedDataArchiveBufferSize;
		Ar.Serialize(SharedDataArchiveBuffer.GetData(), SharedDataArchiveBufferSize);
#endif
	}
	else
	{
		// Counting, etc
		Ar << SharedDataBuffer;

#if WITH_EDITORONLY_DATA
		Ar << SharedDataArchiveBuffer;
#endif
	}
}

bool UAnimNextAnimationGraph::LoadFromArchiveBuffer(const TArray<uint8>& InSharedDataArchiveBuffer)
{
	using namespace UE::AnimNext;

	// Reconstruct our graph shared data
	FMemoryReader GraphSharedDataArchive(InSharedDataArchiveBuffer);
	FTraitReader TraitReader(GraphReferencedObjects, GraphSharedDataArchive);

	const FTraitReader::EErrorState ErrorState = TraitReader.ReadGraph(SharedDataBuffer);
	if (ErrorState == FTraitReader::EErrorState::None)
	{
		for(int32 EntryPointIndex = 0; EntryPointIndex < EntryPoints.Num(); ++EntryPointIndex)
		{
			const FAnimNextGraphEntryPoint& EntryPoint = EntryPoints[EntryPointIndex];
			ResolvedRootTraitHandles.Add(EntryPoint.EntryPointName, TraitReader.ResolveEntryPointHandle(EntryPoint.RootTraitHandle));
			ResolvedEntryPoints.Add(EntryPoint.EntryPointName, EntryPointIndex);
		}

		// Make sure our execute method is registered
		FRigUnit_AnimNextGraphEvaluator::RegisterExecuteMethod(ExecuteDefinition);
		return true;
	}
	else
	{
		SharedDataBuffer.Empty(0);
		ResolvedRootTraitHandles.Add(DefaultEntryPoint, FAnimNextTraitHandle());
		return false;
	}
}

#if WITH_EDITORONLY_DATA
void UAnimNextAnimationGraph::FreezeGraphInstances()
{
	FScopeLock Lock(&GraphInstancesLock);

	TSet<FAnimNextGraphInstance*> GraphInstancesCopy = GraphInstances;
	for (FAnimNextGraphInstance* GraphInstance : GraphInstancesCopy)
	{
		GraphInstance->Freeze();
	}
}

void UAnimNextAnimationGraph::ThawGraphInstances()
{
	FScopeLock Lock(&GraphInstancesLock);

	TSet<FAnimNextGraphInstance*> GraphInstancesCopy = GraphInstances;
	for (FAnimNextGraphInstance* GraphInstance : GraphInstancesCopy)
	{
		GraphInstance->Thaw();
	}
}
#endif
