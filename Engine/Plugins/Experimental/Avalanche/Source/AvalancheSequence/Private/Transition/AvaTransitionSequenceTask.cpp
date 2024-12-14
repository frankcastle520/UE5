// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transition/AvaTransitionSequenceTask.h"
#include "AvaTransitionUtils.h"
#include "StateTreeExecutionContext.h"
#include "Transition/AvaTransitionSequenceUtils.h"

void FAvaTransitionSequenceTask::PostLoad(FStateTreeDataView InInstanceDataView)
{
	Super::PostLoad(InInstanceDataView);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (QueryType_DEPRECATED != EAvaTransitionSequenceQueryType::None)
	{
		if (FInstanceDataType* InstanceData = UE::AvaTransition::TryGetInstanceData(*this, InInstanceDataView))
		{
			InstanceData->WaitType = WaitType_DEPRECATED;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

EAvaTransitionSequenceWaitType FAvaTransitionSequenceTask::GetWaitType(FStateTreeExecutionContext& InContext) const
{
	return InContext.GetInstanceData(*this).WaitType;
}
