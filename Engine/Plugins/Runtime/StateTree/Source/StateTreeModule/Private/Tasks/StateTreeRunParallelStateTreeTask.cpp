// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/StateTreeRunParallelStateTreeTask.h"

#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "StateTree"

FStateTreeRunParallelStateTreeTask::FStateTreeRunParallelStateTreeTask()
{
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldAffectTransitions = true;
}

EStateTreeRunStatus FStateTreeRunParallelStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transitions) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FStateTreeReference& StateTreeToRun = GetStateTreeToRun(Context, InstanceData);
	if (!StateTreeToRun.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Share event queue with parent tree.
	if (FStateTreeInstanceData* OuterInstanceData = Context.GetMutableInstanceData())
	{
		InstanceData.TreeInstanceData.SetSharedEventQueue(OuterInstanceData->GetSharedMutableEventQueue());
	}
	
	InstanceData.RunningStateTree = StateTreeToRun.GetStateTree();
	FStateTreeExecutionContext ParallelTreeContext(Context, *InstanceData.RunningStateTree, InstanceData.TreeInstanceData);
	if (!ParallelTreeContext.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	return ParallelTreeContext.Start(&StateTreeToRun.GetParameters());
}

EStateTreeRunStatus FStateTreeRunParallelStateTreeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.RunningStateTree)
	{
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeExecutionContext ParallelTreeContext(Context, *InstanceData.RunningStateTree, InstanceData.TreeInstanceData);
	if (!ParallelTreeContext.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	return ParallelTreeContext.TickUpdateTasks(DeltaTime);
}

void FStateTreeRunParallelStateTreeTask::TriggerTransitions(FStateTreeExecutionContext& Context) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.RunningStateTree)
	{
		return;
	}

	FStateTreeExecutionContext ParallelTreeContext(Context, *InstanceData.RunningStateTree, InstanceData.TreeInstanceData);
	if (!ParallelTreeContext.IsValid())
	{
		return;
	}

	ParallelTreeContext.TickTriggerTransitions();
}

void FStateTreeRunParallelStateTreeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (!InstanceData.RunningStateTree)
	{
		return;
	}

	FStateTreeExecutionContext ParallelTreeContext(Context, *InstanceData.RunningStateTree, InstanceData.TreeInstanceData);
	if (!ParallelTreeContext.IsValid())
	{
		return;
	}

	ParallelTreeContext.Stop();
}

const FStateTreeReference& FStateTreeRunParallelStateTreeTask::GetStateTreeToRun(FStateTreeExecutionContext& Context, FInstanceDataType& InstanceData) const
{
	if (StateTreeOverrideTag.IsValid())
	{
		if (const FStateTreeReference* Override = Context.GetLinkedStateTreeOverrideForTag(StateTreeOverrideTag))
		{
			return *Override;
		}
	}

	return InstanceData.StateTree;
}

#if WITH_EDITOR
EDataValidationResult FStateTreeRunParallelStateTreeTask::Compile(FStateTreeDataView InstanceDataView, TArray<FText>& ValidationMessages)
{
	TransitionHandlingPriority = EventHandlingPriority;

	return EDataValidationResult::Valid;
}

void FStateTreeRunParallelStateTreeTask::PostEditInstanceDataChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FStateTreeRunParallelStateTreeTaskInstanceData, StateTree))
	{
		InstanceDataView.GetMutable<FInstanceDataType>().StateTree.SyncParameters();
	}
}

void FStateTreeRunParallelStateTreeTask::PostLoad(FStateTreeDataView InstanceDataView)
{
	if (FInstanceDataType* DataType = InstanceDataView.GetMutablePtr<FInstanceDataType>())
	{
		DataType->StateTree.SyncParameters();
	}
}

FText FStateTreeRunParallelStateTreeTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* InstanceData = InstanceDataView.GetPtr<FInstanceDataType>();
	check(InstanceData);

	FText StateTreeValue = BindingLookup.GetBindingSourceDisplayName(FStateTreePropertyPath(ID, GET_MEMBER_NAME_CHECKED(FInstanceDataType, StateTree)), Formatting);
	if (StateTreeValue.IsEmpty())
	{
		StateTreeValue = FText::FromString(GetNameSafe(InstanceData->StateTree.GetStateTree()));
	}

	const FText Format = (Formatting == EStateTreeNodeFormatting::RichText)
		? LOCTEXT("RunParallelRich", "<b>Run Parallel</> {Asset}")
		: LOCTEXT("RunParallel", "Run Parallel {Asset}");

	return FText::FormatNamed(Format,
		TEXT("Asset"), StateTreeValue);
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
