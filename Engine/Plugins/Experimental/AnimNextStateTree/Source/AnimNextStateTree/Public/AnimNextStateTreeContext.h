// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextStateTreeContext.generated.h"

namespace UE::AnimNext
{
struct FTraitStackBinding;
struct FExecutionContext;
struct FStateTreeTrait;
}

class UAnimNextAnimationGraph;
struct FAlphaBlendArgs;

USTRUCT()
struct FAnimNextStateTreeTraitContext
{	
	GENERATED_BODY()
	
	friend UE::AnimNext::FStateTreeTrait;
	
	FAnimNextStateTreeTraitContext() {}

	bool PushAnimationGraphOntoBlendStack(TNonNullPtr<UAnimNextAnimationGraph> InAnimationGraph, const FAlphaBlendArgs& InBlendArguments) const;
protected:
	FAnimNextStateTreeTraitContext(UE::AnimNext::FExecutionContext& InContext, const UE::AnimNext::FTraitStackBinding* InBinding) : Context(&InContext), Binding(InBinding) {}
protected:
	UE::AnimNext::FExecutionContext* Context = nullptr;
	const UE::AnimNext::FTraitStackBinding* Binding = nullptr;
};

