// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeConditionBase.h"

#include "StateTreeAIConditionBase.generated.h"

// Base class of all AI condition that expect to be run on an AIController or derived class 
USTRUCT(meta = (Hidden, Category = "AI"))
struct FStateTreeAIConditionBase : public FStateTreeConditionBase
{
	GENERATED_BODY()
};