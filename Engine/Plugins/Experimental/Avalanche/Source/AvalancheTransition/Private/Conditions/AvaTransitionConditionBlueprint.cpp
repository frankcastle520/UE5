// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/AvaTransitionConditionBlueprint.h"
#include "Misc/ScopeExit.h"

bool UAvaTransitionConditionBlueprint::TestCondition(FStateTreeExecutionContext& InContext) const
{
	if (bHasTestCondition)
	{
		BehaviorInstanceCache.SetCachedInstanceDataFromContext(InContext);
		ON_SCOPE_EXIT
		{
			BehaviorInstanceCache.ClearCachedInstanceData();
		};
		return Super::TestCondition(InContext);
	}
	return false;
}

const FAvaTransitionBehaviorInstanceCache& UAvaTransitionConditionBlueprint::GetBehaviorInstanceCache() const
{
	return BehaviorInstanceCache;
}
