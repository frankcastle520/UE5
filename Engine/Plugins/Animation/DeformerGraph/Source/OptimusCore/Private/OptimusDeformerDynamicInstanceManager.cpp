// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformerDynamicInstanceManager.h"

#include "ControlRig.h"
#include "IControlRigObjectBinding.h"
#include "OptimusDeformerInstance.h"
#include "ControlRig/RigUnit_Optimus.h"

void UOptimusDeformerDynamicInstanceManager::AllocateResources()
{
	// Typically called during recreate render state
	
	DefaultInstance->AllocateResources();

	for (TPair<FGuid, TObjectPtr<UOptimusDeformerInstance>>& GuidToInstance : GuidToRigDeformerInstanceMap)
	{
		GuidToInstance.Value->AllocateResources();
	}
}

void UOptimusDeformerDynamicInstanceManager::ReleaseResources()
{
	// Typically called during recreate render state
	
	DefaultInstance->ReleaseResources();
	
	for (TPair<FGuid, TObjectPtr<UOptimusDeformerInstance>>& GuidToInstance : GuidToRigDeformerInstanceMap)
	{
		GuidToInstance.Value->ReleaseResources();
	}
}

void UOptimusDeformerDynamicInstanceManager::EnqueueWork(FEnqueueWorkDesc const& InDesc)
{
	// Runs during UWorld::SendAllEndOfFrameUpdates

	for (FGuid Guid : RigDeformerInstancePendingInit)
	{
		if (TObjectPtr<UOptimusDeformerInstance>* InstancePtr = GuidToRigDeformerInstanceMap.Find(Guid))
		{
			(*InstancePtr)->AllocateResources();
		}
	}

	RigDeformerInstancePendingInit.Reset();
	
	// Enqueue work

	// Making sure instances in the queue are dispatched sequentially
	uint8 NumComputeGraphsPossiblyEnqueued = 0;
	// Used to inform later instances whether specific buffers have valid data in them
	EMeshDeformerOutputBuffer OutputBuffers = EMeshDeformerOutputBuffer::None;
	
	auto EnqueueInstance = [&NumComputeGraphsPossiblyEnqueued, &InDesc, &OutputBuffers](UOptimusDeformerInstance* Instance)
	{
		Instance->OutputBuffersFromPreviousInstances = OutputBuffers;
		OutputBuffers |= Instance->GetOutputBuffers();
		
		Instance->GraphSortPriorityOffset = NumComputeGraphsPossiblyEnqueued;
		NumComputeGraphsPossiblyEnqueued += Instance->ComputeGraphExecInfos.Num();
		
		Instance->EnqueueWork(InDesc);
	};

	static const EOptimusDeformerExecutionPhase Phases[] = {
		EOptimusDeformerExecutionPhase::BeforeDefaultDeformer,
		EOptimusDeformerExecutionPhase::OverrideDefaultDeformer,
		EOptimusDeformerExecutionPhase::AfterDefaultDeformer,
	};

	for (EOptimusDeformerExecutionPhase Phase : Phases)
	{
		if (TMap<int32, TArray<FGuid>>* ExecutionGroupQueueMapPtr = InstanceQueueMap.Find(Phase))
		{
			TArray<int32> SortedExecutionGroups;
			ExecutionGroupQueueMapPtr->GenerateKeyArray(SortedExecutionGroups);
			SortedExecutionGroups.Sort();

			if (Phase == EOptimusDeformerExecutionPhase::OverrideDefaultDeformer)
			{
				// The last instance in the override queue is the one getting used
				int32 LastOverrideExecutionGroup = SortedExecutionGroups.Last();
				FGuid LastOverrideInstanceGuid = (*ExecutionGroupQueueMapPtr)[LastOverrideExecutionGroup].Last();

				if (TObjectPtr<UOptimusDeformerInstance>* InstancePtr = GuidToRigDeformerInstanceMap.Find(LastOverrideInstanceGuid))
				{
					EnqueueInstance((*InstancePtr));
				}
			}
			else
			{
				for (int32 ExecutionGroup : SortedExecutionGroups)
				{
					for (FGuid Guid : (*ExecutionGroupQueueMapPtr)[ExecutionGroup])
					{
						if (TObjectPtr<UOptimusDeformerInstance>* InstancePtr = GuidToRigDeformerInstanceMap.Find(Guid))
						{
							EnqueueInstance((*InstancePtr));
						}
					}
				}
			}
		}
		else if (Phase == EOptimusDeformerExecutionPhase::OverrideDefaultDeformer)
		{
			// Use the default instance if there is no override
			EnqueueInstance(DefaultInstance);
		}
	}

	InstanceQueueMap.Reset();
}

EMeshDeformerOutputBuffer UOptimusDeformerDynamicInstanceManager::GetOutputBuffers() const
{
	// Since instances can be added dynamically, no way to know in advance if some of these are not written to, so just declare all of them
	EMeshDeformerOutputBuffer Result = EMeshDeformerOutputBuffer::SkinnedMeshPosition | EMeshDeformerOutputBuffer::SkinnedMeshTangents | EMeshDeformerOutputBuffer::SkinnedMeshVertexColor;
	
	return Result;
}

UMeshDeformerInstance* UOptimusDeformerDynamicInstanceManager::GetInstanceForSourceDeformer()
{
	return DefaultInstance;
}


void UOptimusDeformerDynamicInstanceManager::OnControlRigBeginDestroy(UControlRig* InControlRig)
{
	if (TArray<FGuid>* Instances = RigToInstanceGuidsMap.Find(InControlRig))
	{
		TArray<FGuid> InstancesToRemove = *Instances;
		for (FGuid Guid : InstancesToRemove)
		{
			if (TObjectPtr<UOptimusDeformerInstance>* InstancePtr = GuidToRigDeformerInstanceMap.Find(Guid))
			{
				(*InstancePtr)->ReleaseResources();
			}

			GuidToRigDeformerInstanceMap.Remove(Guid);
		}
	}
	
	RigToInstanceGuidsMap.Remove(InControlRig);
	
	InControlRig->OnBeginDestroy().RemoveAll(this);
}

void UOptimusDeformerDynamicInstanceManager::BeginDestroy()
{
	TArray<TWeakObjectPtr<UControlRig>> Rigs;
	RigToInstanceGuidsMap.GenerateKeyArray(Rigs);

	// Release resources should have been called, so just unregister callbacks for good measure
	
	for (TWeakObjectPtr<UControlRig>& Rig : Rigs)
	{
		if (Rig.IsValid())
		{
			Rig->OnBeginDestroy().RemoveAll(this);
		}
	}
	
	Super::BeginDestroy();
}


void UOptimusDeformerDynamicInstanceManager::AddRigDeformer(UControlRig* InControlRig, FGuid InInstanceGuid, UOptimusDeformer* InDeformer)
{
	check(IsInGameThread());
	
	if (ensure(!GuidToRigDeformerInstanceMap.Contains(InInstanceGuid)))
	{
		UOptimusDeformerInstance* DeformerInstance = InDeformer->CreateOptimusInstance(CastChecked<USkeletalMeshComponent>(GetOuter()), nullptr);
		GuidToRigDeformerInstanceMap.Add(InInstanceGuid, DeformerInstance);
		RigDeformerInstancePendingInit.Add(InInstanceGuid);

		if (TArray<FGuid>* GuidArray = RigToInstanceGuidsMap.Find(InControlRig))
		{
			GuidArray->Add(InInstanceGuid);
		}
		else
		{
			RigToInstanceGuidsMap.Add(InControlRig, {InInstanceGuid});
			
			// First time for this control rig, register some callbacks as well
			check(!InControlRig->OnBeginDestroy().IsBoundToObject(this));
			// Assuming owning component of the rig cannot change
			InControlRig->OnBeginDestroy().AddUObject(this, &UOptimusDeformerDynamicInstanceManager::OnControlRigBeginDestroy);

		}
	}
}

UOptimusDeformerInstance* UOptimusDeformerDynamicInstanceManager::GetRigDeformer(FGuid InInstanceGuid)
{
	TObjectPtr<UOptimusDeformerInstance>* InstancePtr = GuidToRigDeformerInstanceMap.Find(InInstanceGuid);
	
	if (InstancePtr)
	{
		return *InstancePtr;
	}

	return nullptr;
}


void UOptimusDeformerDynamicInstanceManager::EnqueueRigDeformer(FGuid InInstanceGuid, EOptimusDeformerExecutionPhase InExecutionPhase, int32 InExecutionGroup)
{
	// Typically called from anim thread, but there shouldn't be concurrent access to this queue. All rigs running on the current mesh should run sequentially.
	
	
	TArray<FGuid>& InstanceQueueRef = InstanceQueueMap.FindOrAdd(InExecutionPhase).FindOrAdd(InExecutionGroup);
	
	// If we ever get duplicates, it means extra unnecessary instances were added via extra control rig evaluations triggered by user actions like moving a control.
	// So let's invalidate those instances.
	if (FGuid* BadInstance = InstanceQueueRef.FindByKey(InInstanceGuid))
	{
		*BadInstance = FGuid();
	}
	InstanceQueueRef.Add(InInstanceGuid);
}

