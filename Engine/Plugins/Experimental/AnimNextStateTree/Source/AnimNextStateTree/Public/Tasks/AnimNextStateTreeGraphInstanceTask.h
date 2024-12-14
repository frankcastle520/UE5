// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextStateTreeContext.h"
#include "AnimNextStateTreeTypes.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "AlphaBlend.h"

#include "AnimNextStateTreeGraphInstanceTask.generated.h"

USTRUCT()
struct ANIMNEXTSTATETREE_API FAnimNextGraphInstanceTaskInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Animation)
	TObjectPtr<UAnimNextAnimationGraph> AnimationGraph = nullptr;

	UPROPERTY(EditAnywhere, Category = Animation)
	FAlphaBlendArgs BlendOptions;

	UPROPERTY(EditAnywhere, Category = Animation)
	bool bContinueTicking = true;
};

// Basic task pushing AnimationGraph onto blend stack
USTRUCT(meta = (DisplayName = "AnimNext Graph"))
struct ANIMNEXTSTATETREE_API FAnimNextStateTreeGraphInstanceTask : public FAnimNextStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAnimNextGraphInstanceTaskInstanceData;

	FAnimNextStateTreeGraphInstanceTask();
	
	virtual bool Link(FStateTreeLinker& Linker) override;
protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

public:
	TStateTreeExternalDataHandle<FAnimNextStateTreeTraitContext> TraitContextHandle;
};
