// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTransitionTree.h"

#include "StateTreeTaskBase.h"

FAvaTagHandle UAvaTransitionTree::GetTransitionLayer() const
{
	return TransitionLayer;
}

void UAvaTransitionTree::SetTransitionLayer(FAvaTagHandle InTransitionLayer)
{
	TransitionLayer = InTransitionLayer;
}

bool UAvaTransitionTree::IsEnabled() const
{
	return bEnabled;
}

void UAvaTransitionTree::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
}

bool UAvaTransitionTree::ContainsTask(const UScriptStruct* InTaskStruct) const
{
	for (const FCompactStateTreeState& State : GetStates())
	{
		if (!State.bEnabled)
		{
			continue;
		}

		for (int32 TaskIndex = State.TasksBegin; TaskIndex < State.TasksBegin + State.TasksNum; ++TaskIndex)
		{
			const FConstStructView Node = GetNodes()[TaskIndex];

			const UScriptStruct* TaskStruct = Node.GetScriptStruct();
			if (TaskStruct && TaskStruct->IsChildOf(InTaskStruct))
			{
				const FStateTreeTaskBase* TaskNode = Node.GetPtr<const FStateTreeTaskBase>();
				if (TaskNode && TaskNode->bTaskEnabled)
				{
					return true;
				}
			}
		}
	}

	return false;
}
