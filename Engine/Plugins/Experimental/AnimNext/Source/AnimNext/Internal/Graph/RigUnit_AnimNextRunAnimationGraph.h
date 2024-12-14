// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextExecuteContext.h"
#include "Graph/AnimNext_LODPose.h"
#include "Graph/RigUnit_AnimNextBase.h"
#include "Graph/AnimNextGraphInstancePtr.h"

#include "RigUnit_AnimNextRunAnimationGraph.generated.h"

/** Runs an animation graph */
USTRUCT(meta=(DisplayName="Run Graph", Category="Animation Graph", NodeColor="0, 1, 1", Keywords="Trait,Stack"))
struct ANIMNEXT_API FRigUnit_AnimNextRunAnimationGraph : public FRigUnit_AnimNextBase
{
	GENERATED_BODY()

	RIGVM_METHOD()
	void Execute();

	// Graph to run
	UPROPERTY(EditAnywhere, Category = "Graph", meta = (Input))
	TObjectPtr<UAnimNextAnimationGraph> Graph;

	// Instance used to hold graph state
	UPROPERTY(EditAnywhere, Category = "Graph", meta = (Input, Output))
	FAnimNextGraphInstancePtr Instance;

	// LOD to run the graph at
	UPROPERTY(EditAnywhere, Category = "Graph", meta = (Input))
	int32 LOD = 0;

	// Reference pose for the graph
	UPROPERTY(EditAnywhere, Category = "Graph", meta = (Input))
	FAnimNextGraphReferencePose ReferencePose;

	// Pose result
	UPROPERTY(EditAnywhere, Category = "Graph", meta = (Output))
	FAnimNextGraphLODPose Result;

	// The execution result
	UPROPERTY(EditAnywhere, DisplayName = "Execute", Category = "BeginExecution", meta = (Input, Output))
	FAnimNextExecuteContext ExecuteContext;
};
