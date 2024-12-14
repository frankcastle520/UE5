// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Graph/AnimNextGraphEntryPoint.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "Graph/RigUnit_AnimNextGraphRoot.h"
#include "RigVMCore/RigVM.h"
#include "TraitCore/TraitPtr.h"
#include "TraitCore/TraitHandle.h"
#include "TraitCore/EntryPointHandle.h"
#include "Graph/RigUnit_AnimNextGraphEvaluator.h"
#include "RigVMHost.h"

#include "AnimNextAnimationGraph.generated.h"

class UEdGraph;
class UAnimNextModule;
class UAnimGraphNode_AnimNextGraph;
struct FAnimNode_AnimNextGraph;
struct FRigUnit_AnimNextGraphEvaluator;
struct FAnimNextGraphInstancePtr;
struct FAnimNextGraphInstance;
struct FAnimNextScheduleGraphTask;
struct FAnimNextEditorParam;
struct FAnimNextParam;
struct FAnimNextModuleInstance;

namespace UE::AnimNext
{
	struct FContext;
	struct FExecutionContext;
	class FAnimNextModuleImpl;
	struct FTestUtils;
	struct FParametersProxy;
	struct FPlayAnimSlotTrait;
}

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::Editor
{
	class FModuleEditor;
	class FVariableCustomization;
}

namespace UE::AnimNext::Graph
{
	extern ANIMNEXT_API const FName EntryPointName;
	extern ANIMNEXT_API const FName ResultName;
}

// A user-created collection of animation logic & data
UCLASS(BlueprintType)
class ANIMNEXT_API UAnimNextAnimationGraph : public UAnimNextDataInterface
{
	GENERATED_BODY()

public:
	UAnimNextAnimationGraph(const FObjectInitializer& ObjectInitializer);

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;

	// Allocates an instance of the graph
	// @param	OutInstance			The instance to allocate data for
	// @param	InModuleInstance	The module instance to use
	// @param	InEntryPoint		The entry point to use. If this is NAME_None then the default entry point for this graph is used
	void AllocateInstance(FAnimNextGraphInstancePtr& OutInstance, FAnimNextModuleInstance* InModuleInstance = nullptr, FName InEntryPoint = NAME_None) const;

	// Allocates an instance of the graph with the specified parent graph instance
	// @param	InParentGraphInstance		The parent graph instance to use
	// @param	OutInstance					The instance to allocate data for
	// @param	InEntryPoint				The entry point to use. If this is NAME_None then the default entry point for this graph is used
	void AllocateInstance(FAnimNextGraphInstance& InParentGraphInstance, FAnimNextGraphInstancePtr& OutInstance, FName InEntryPoint = NAME_None) const;

protected:

	// Loads the graph data from the provided archive buffer and returns true on success, false otherwise
	bool LoadFromArchiveBuffer(const TArray<uint8>& SharedDataArchiveBuffer);

	// Allocates an instance of the graph with an optional parent graph instance
	void AllocateInstanceImpl(FAnimNextModuleInstance* InModuleInstance, FAnimNextGraphInstance* InParentGraphInstance, FAnimNextGraphInstancePtr& OutInstance, FName InEntryPoint) const;

#if WITH_EDITORONLY_DATA
	// During graph compilation, if we have existing graph instances, we freeze them by releasing their memory before thawing them
	// Freezing is a partial release of resources that retains the necessary information to re-create things safely
	void FreezeGraphInstances();

	// During graph compilation, once compilation is done we thaw existing graph instances to reallocate their memory
	void ThawGraphInstances();
#endif
	
	friend class UAnimNextAnimationGraphFactory;
	friend class UAnimNextAnimationGraph_EditorData;
	friend class UAnimNextVariableEntry;
	friend struct UE::AnimNext::UncookedOnly::FUtils;
	friend class UE::AnimNext::Editor::FModuleEditor;
	friend struct UE::AnimNext::FTestUtils;
	friend FAnimNextGraphInstancePtr;
	friend FAnimNextGraphInstance;
	friend class UAnimGraphNode_AnimNextGraph;
	friend UE::AnimNext::FExecutionContext;
	friend struct FAnimNextScheduleGraphTask;
	friend UE::AnimNext::FAnimNextModuleImpl;
	friend class UE::AnimNext::Editor::FVariableCustomization;
	friend struct UE::AnimNext::FParametersProxy;
	friend struct UE::AnimNext::FPlayAnimSlotTrait;

#if WITH_EDITORONLY_DATA
	mutable FCriticalSection GraphInstancesLock;

	// This is a list of live graph instances that have been allocated, used in the editor to reset instances when we re-compile/live edit
	mutable TSet<FAnimNextGraphInstance*> GraphInstances;
#endif

	// This is the execute method definition used by a graph to evaluate latent pins
	UPROPERTY()
	FAnimNextGraphEvaluatorExecuteDefinition ExecuteDefinition;

	// Data for each entry point in this graph
	UPROPERTY()
	TArray<FAnimNextGraphEntryPoint> EntryPoints;

	// This is a resolved handle to the root trait in our graph, for each entry point 
	TMap<FName, FAnimNextTraitHandle> ResolvedRootTraitHandles;

	// This is an index into EntryPoints, for each entry point
	TMap<FName, int32> ResolvedEntryPoints;

	// This is the graph shared data used by the trait system, the output of FTraitReader
	// We de-serialize manually into this buffer from the archive buffer, this is never saved on disk
	TArray<uint8> SharedDataBuffer;

	// This is a list of all referenced UObjects in the graph shared data
	// We collect all the references here to make it quick and easy for the GC to query them
	// It means that object references in the graph shared data are not visited at runtime by the GC (they are immutable)
	// The shared data serialization archive stores indices to these to perform UObject serialization
	UPROPERTY()
	TArray<TObjectPtr<UObject>> GraphReferencedObjects;

	// The entry point that this graph defaults to using
	UPROPERTY(EditAnywhere, Category = "Graph")
	FName DefaultEntryPoint = FRigUnit_AnimNextGraphRoot::DefaultEntryPoint;

	// Default state for this graph
	UPROPERTY()
	FAnimNextGraphState DefaultState;

#if WITH_EDITORONLY_DATA
	// This buffer holds the output of the FTraitWriter post compilation
	// We serialize it manually and it is discarded at runtime
	TArray<uint8> SharedDataArchiveBuffer;
#endif
};
