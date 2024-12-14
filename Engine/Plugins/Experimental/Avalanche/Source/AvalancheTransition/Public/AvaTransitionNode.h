// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionTypes.h"

class UAvaTransitionSubsystem;
struct FAvaTransitionContext;
struct FStateTreeLinker;

class FAvaTransitionNode
{
public:
	AVALANCHETRANSITION_API bool LinkNode(FStateTreeLinker& InLinker);

	TStateTreeExternalDataHandle<FAvaTransitionContext> TransitionContextHandle;

	TStateTreeExternalDataHandle<UAvaTransitionSubsystem> TransitionSubsystemHandle;
};
