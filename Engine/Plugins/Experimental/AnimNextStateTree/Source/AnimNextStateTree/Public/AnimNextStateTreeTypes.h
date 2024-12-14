// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h" 
#include "StateTreeTaskBase.h"

#include "AnimNextStateTreeTypes.generated.h"

/**
 * Base struct for all AnimNext StateTree Evaluators.
 */
USTRUCT(meta = (DisplayName = "AnimNext Evaluator Base", Hidden))
struct ANIMNEXTSTATETREE_API FAnimNextStateTreeEvaluatorBase : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()
};

/**
 * Base struct for all AnimNext StateTree Tasks.
 */
USTRUCT(meta = (DisplayName = "AnimNext Task Base", Hidden))
struct ANIMNEXTSTATETREE_API FAnimNextStateTreeTaskBase : public FStateTreeTaskBase
{
	GENERATED_BODY()
};
