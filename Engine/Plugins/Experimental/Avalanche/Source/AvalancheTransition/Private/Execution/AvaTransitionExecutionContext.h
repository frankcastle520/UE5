// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "StateTreeExecutionContext.h"

struct FAvaTransitionBehaviorInstance;

struct FAvaTransitionExecutionContext : FStateTreeExecutionContext
{
	FAvaTransitionExecutionContext(const FAvaTransitionBehaviorInstance& InBehaviorInstance, UObject& InOwner, const UStateTree& InStateTree, FStateTreeInstanceData& InInstanceData);

	void SetSceneDescription(FString&& InSceneDescription);

	const FAvaTransitionBehaviorInstance* GetBehaviorInstance() const
	{
		return &BehaviorInstance;
	}

protected:
	//~ Begin FStateTreeExecutionContext
	virtual FString GetInstanceDescription() const override;
	//~ End FStateTreeExecutionContext

private:
	const FAvaTransitionBehaviorInstance& BehaviorInstance;

	FString SceneDescription;
};
