// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transition/Tasks/AvaSceneTask.h"
#include "AvaSceneSubsystem.h"
#include "AvaTransitionContext.h"
#include "AvaTransitionUtils.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

void FAvaSceneTask::PostLoad(FStateTreeDataView InInstanceDataView)
{
	Super::PostLoad(InInstanceDataView);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (TagAttribute_DEPRECATED.IsValid())
	{
		if (FInstanceDataType* InstanceData = UE::AvaTransition::TryGetInstanceData(*this, InInstanceDataView))
		{
			InstanceData->TagAttribute = TagAttribute_DEPRECATED;
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FAvaSceneTask::Link(FStateTreeLinker& InLinker)
{
	Super::Link(InLinker);
	InLinker.LinkExternalData(SceneSubsystemHandle);
	return true;
}

IAvaSceneInterface* FAvaSceneTask::GetScene(FStateTreeExecutionContext& InContext) const
{
	const UAvaSceneSubsystem& SceneSubsystem       = InContext.GetExternalData(SceneSubsystemHandle);
	const FAvaTransitionContext& TransitionContext = InContext.GetExternalData(TransitionContextHandle);
	const FAvaTransitionScene* TransitionScene     = TransitionContext.GetTransitionScene();

	return TransitionScene ? SceneSubsystem.GetSceneInterface(TransitionScene->GetLevel()) : nullptr;
}
