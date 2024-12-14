// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/AnimNextStateTreeGraphInstanceTask.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"

FAnimNextStateTreeGraphInstanceTask::FAnimNextStateTreeGraphInstanceTask()
{
}

bool FAnimNextStateTreeGraphInstanceTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TraitContextHandle);	
	return true;
}

EStateTreeRunStatus FAnimNextStateTreeGraphInstanceTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FAnimNextStateTreeTraitContext& ExecContext = Context.GetExternalData(TraitContextHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (ExecContext.PushAnimationGraphOntoBlendStack(InstanceData.AnimationGraph.Get(), InstanceData.BlendOptions))
	{
		return EStateTreeRunStatus::Running;	
	}
	
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FAnimNextStateTreeGraphInstanceTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.bContinueTicking)
	{
		return EStateTreeRunStatus::Running;
	}
	
	return EStateTreeRunStatus::Succeeded;
}

void FAnimNextStateTreeGraphInstanceTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FAnimNextStateTreeTaskBase::ExitState(Context, Transition);
}
