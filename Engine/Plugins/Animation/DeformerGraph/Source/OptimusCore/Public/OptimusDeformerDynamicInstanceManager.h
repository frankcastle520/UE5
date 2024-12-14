// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerInstance.h"
#include "OptimusDeformerDynamicInstanceManager.generated.h"


class UControlRig;
class UOptimusDeformerInstance;
class UOptimusDeformer;

UENUM()
enum class EOptimusDeformerExecutionPhase : uint8
{
	AfterDefaultDeformer = 0,
	OverrideDefaultDeformer = 1,
	BeforeDefaultDeformer = 2,
};

/** 
 * Enables composition of multiple deformer instances dynamically
 */
UCLASS()
class OPTIMUSCORE_API UOptimusDeformerDynamicInstanceManager : public UMeshDeformerInstance
{
	GENERATED_BODY()

public:
	/** Called to allocate any persistent render resources */
	void AllocateResources() override;;

	/** Called when persistent render resources should be released */
	void ReleaseResources() override;

	/** Enqueue the mesh deformer workload on a scene. */
	void EnqueueWork(FEnqueueWorkDesc const& InDesc) override;

	/** Return the buffers that this deformer can potentially write to */
	EMeshDeformerOutputBuffer GetOutputBuffers() const override;

	/** InstanceManager is an intermediate instance, call this function to get the instance for the deformer that created this instance manager */
	UMeshDeformerInstance* GetInstanceForSourceDeformer() override;

	/** Remove associated deformer instances when the rig is removed */
	void OnControlRigBeginDestroy(UControlRig* InControlRig);

	void BeginDestroy() override;

	void AddRigDeformer(UControlRig* InControlRig, FGuid InInstanceGuid, UOptimusDeformer* InDeformer);
	UOptimusDeformerInstance* GetRigDeformer(FGuid InInstanceGuid);
	void EnqueueRigDeformer(FGuid InInstanceGuid, EOptimusDeformerExecutionPhase InExecutionPhase, int32 InExecutionGroup);
	
	UPROPERTY()
	TObjectPtr<UOptimusDeformerInstance> DefaultInstance;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UOptimusDeformerInstance>> GuidToRigDeformerInstanceMap;

	TMap<TWeakObjectPtr<UControlRig>, TArray<FGuid>> RigToInstanceGuidsMap;

	// Freshly created deformer instances should be initialized before dispatch 
	TArray<FGuid> RigDeformerInstancePendingInit;

	// Instances per execution group per execution phase
	TMap<EOptimusDeformerExecutionPhase, TMap<int32, TArray<FGuid>>> InstanceQueueMap;
};





