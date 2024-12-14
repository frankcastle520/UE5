// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTransitionAttributeLibrary.h"
#include "AvaSceneState.h"
#include "AvaSceneSubsystem.h"
#include "AvaTransitionContext.h"
#include "AvaTransitionLayer.h"
#include "AvaTransitionLayerUtils.h"
#include "AvaTransitionSubsystem.h"
#include "Behavior/AvaTransitionBehaviorInstance.h"
#include "Behavior/AvaTransitionBehaviorInstanceCache.h"
#include "IAvaSceneInterface.h"
#include "IAvaTransitionNodeInterface.h"

namespace UE::Ava::Private
{
	UAvaSceneState* GetSceneState(UObject* InTransitionNode)
	{
		const IAvaTransitionNodeInterface* NodeInterface = Cast<IAvaTransitionNodeInterface>(InTransitionNode);
		if (!NodeInterface)
		{
			return nullptr;
		}

		const FAvaTransitionContext* TransitionContext = NodeInterface->GetBehaviorInstanceCache().GetTransitionContext();
		if (!TransitionContext)
		{
			return nullptr;
		}

		const FAvaTransitionScene* TransitionScene = TransitionContext->GetTransitionScene();
		if (!TransitionScene)
		{
			return nullptr;
		}

		if (IAvaSceneInterface* SceneInterface = UAvaSceneSubsystem::FindSceneInterface(TransitionScene->GetLevel()))
		{
			return SceneInterface->GetSceneState();
		}
		return nullptr;
	}
}

bool UAvaTransitionAttributeLibrary::AddTagAttribute(UObject* InTransitionNode, const FAvaTagHandle& InTagHandle)
{
	if (UAvaSceneState* SceneState = UE::Ava::Private::GetSceneState(InTransitionNode))
	{
		return SceneState->AddTagAttribute(InTagHandle);
	}
	return false;
}

bool UAvaTransitionAttributeLibrary::RemoveTagAttribute(UObject* InTransitionNode, const FAvaTagHandle& InTagHandle)
{
	if (UAvaSceneState* SceneState = UE::Ava::Private::GetSceneState(InTransitionNode))
	{
		return SceneState->RemoveTagAttribute(InTagHandle);
	}
	return false;
}

bool UAvaTransitionAttributeLibrary::ContainsTagAttribute(UObject* InTransitionNode, const FAvaTagHandle& InTagHandle)
{
	if (UAvaSceneState* SceneState = UE::Ava::Private::GetSceneState(InTransitionNode))
	{
		return SceneState->ContainsTagAttribute(InTagHandle);
	}
	return false;
}

bool UAvaTransitionAttributeLibrary::AddNameAttribute(UObject* InTransitionNode, FName InName)
{
	if (UAvaSceneState* SceneState = UE::Ava::Private::GetSceneState(InTransitionNode))
	{
		return SceneState->AddNameAttribute(InName);
	}
	return false;
}

bool UAvaTransitionAttributeLibrary::RemoveNameAttribute(UObject* InTransitionNode, FName InName)
{
	if (UAvaSceneState* SceneState = UE::Ava::Private::GetSceneState(InTransitionNode))
	{
		return SceneState->RemoveNameAttribute(InName);
	}
	return false;
}

bool UAvaTransitionAttributeLibrary::ContainsNameAttribute(UObject* InTransitionNode, FName InName)
{
	if (UAvaSceneState* SceneState = UE::Ava::Private::GetSceneState(InTransitionNode))
	{
		return SceneState->ContainsNameAttribute(InName);
	}
	return false;
}
