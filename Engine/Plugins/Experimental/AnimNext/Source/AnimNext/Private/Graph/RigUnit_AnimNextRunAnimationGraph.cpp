// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RigUnit_AnimNextRunAnimationGraph.h"

#include "Units/RigUnitContext.h"
#include "AnimNextStats.h"
#include "IAnimNextModuleInterface.h"
#include "Module/AnimNextModuleInstance.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/RigVMTrait_AnimNextPublicVariables.h"
#include "TraitCore/TraitEventList.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_AnimNextRunAnimationGraph)

DEFINE_STAT(STAT_AnimNext_Run_Graph);

FRigUnit_AnimNextRunAnimationGraph_Execute()
{
	SCOPE_CYCLE_COUNTER(STAT_AnimNext_Run_Graph);

	using namespace UE::AnimNext;

	const FAnimNextModuleContextData& ModuleContextData = ExecuteContext.GetContextData<FAnimNextModuleContextData>();
	FAnimNextModuleInstance& ModuleInstance = ModuleContextData.GetModuleInstance();

	if (!ReferencePose.ReferencePose.IsValid())
	{
		return;
	}

	if(Graph == nullptr)
	{
		Instance.Release();
		return;
	}

	// Lazily (re-)allocate graph instance if required
	if(!Instance.IsValid() || !Instance.UsesAnimationGraph(Graph))
	{
		Graph->AllocateInstance(Instance, &ModuleInstance);

		if(!Instance.IsValid())
		{
			return;
		}
	}

	if(!Instance.IsValid())
	{
		return;
	}

	if(Instance.RequiresPublicVariableBinding())
	{
		// Bind any public variables we find
		TArray<FPublicVariablesTraitToDataInterfaceHostAdapter, TInlineAllocator<4>> Adapters;
		Adapters.Reserve(ExecuteContext.GetTraits().Num());
		TArray<IDataInterfaceHost*, TInlineAllocator<4>> Hosts;
		Hosts.Reserve(ExecuteContext.GetTraits().Num());
		for(const FRigVMTraitScope& TraitScope : ExecuteContext.GetTraits())
		{
			const FRigVMTrait_AnimNextPublicVariables* VariablesTrait = TraitScope.GetTrait<FRigVMTrait_AnimNextPublicVariables>();
			if(VariablesTrait == nullptr)
			{
				continue;
			}

			FPublicVariablesTraitToDataInterfaceHostAdapter& Adapter = Adapters.Emplace_GetRef(*VariablesTrait, TraitScope);
			Hosts.Add(&Adapter);
		}

		Instance.BindPublicVariables(Hosts);
	}

	const UE::AnimNext::FReferencePose& RefPose = ReferencePose.ReferencePose.GetRef<UE::AnimNext::FReferencePose>();

	// TODO: Currently forcing additive flag to false here
	if (Result.LODPose.ShouldPrepareForLOD(RefPose, LOD, false))
	{
		Result.LODPose.PrepareForLOD(RefPose, LOD, true, false);
	}

	ensure(Result.LODPose.LODLevel == LOD);

	// Every graph in a schedule will see the same input events (if they were queued before the schedule started)
	FTraitEventList InputEventList;
	FTraitEventList OutputEventList;

	// A schedule can contain multiple graphs, we copy the input event list since it might be appended to during our update
	{
		FReadScopeLock ReadLock(ModuleInstance.EventListLock);
		InputEventList = ModuleInstance.InputEventList;
	}

	// Track how many input events we started with, we'll append the new ones
	const int32 NumOriginalInputEvents = InputEventList.Num();

	// Internally we use memstack allocation, so we need a mark here
	FMemStack& MemStack = FMemStack::Get();
	FMemMark MemMark(MemStack);

	// We allocate a dummy buffer to trigger the allocation of a large chunk if this is the first mark
	// This reduces churn internally by avoiding a chunk to be repeatedly allocated and freed as we push/pop marks
	MemStack.Alloc(size_t(FPageAllocator::SmallPageSize) + 1, 16);

	IAnimNextModuleInterface& AnimNextModule = IAnimNextModuleInterface::Get();
	AnimNextModule.UpdateGraph(Instance, ExecuteContext.GetDeltaTime(), InputEventList, OutputEventList);
	AnimNextModule.EvaluateGraph(Instance, RefPose, LOD, Result);

	// We might have appended new input/output events, append them
	{
		const int32 NumInputEvents = InputEventList.Num();

		FWriteScopeLock WriteLock(ModuleInstance.EventListLock);

		// Append the new input events
		for (int32 EventIndex = NumOriginalInputEvents; EventIndex < NumInputEvents; ++EventIndex)
		{
			FAnimNextTraitEventPtr& Event = InputEventList[EventIndex];
			if (Event->IsValid())
			{
				ModuleInstance.InputEventList.Push(MoveTemp(Event));
			}
		}

		// Append our output events
		ModuleInstance.OutputEventList.Append(OutputEventList);
	}
}
