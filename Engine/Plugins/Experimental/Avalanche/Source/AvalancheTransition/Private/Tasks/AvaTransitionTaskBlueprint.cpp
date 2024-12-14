// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/AvaTransitionTaskBlueprint.h"

EStateTreeRunStatus UAvaTransitionTaskBlueprint::EnterState(FStateTreeExecutionContext& InExecutionContext, const FStateTreeTransitionResult& InTransition)
{
	BehaviorInstanceCache.SetCachedInstanceDataFromContext(InExecutionContext);
	return Super::EnterState(InExecutionContext, InTransition);
}

void UAvaTransitionTaskBlueprint::ExitState(FStateTreeExecutionContext& InExecutionContext, const FStateTreeTransitionResult& InTransition)
{
	Super::ExitState(InExecutionContext, InTransition);
	BehaviorInstanceCache.ClearCachedInstanceData();
}

const FAvaTransitionBehaviorInstanceCache& UAvaTransitionTaskBlueprint::GetBehaviorInstanceCache() const
{
	return BehaviorInstanceCache;
}
