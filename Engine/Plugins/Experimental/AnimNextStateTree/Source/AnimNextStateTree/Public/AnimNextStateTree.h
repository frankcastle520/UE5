// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTree.h"
#include "Graph/AnimNextAnimationGraph.h"

#include "AnimNextStateTree.generated.h"

namespace UE::AnimNext::UncookedOnly
{
struct FUtils;
}


UCLASS(BlueprintType)
class ANIMNEXTSTATETREE_API UAnimNextStateTree : public UAnimNextAnimationGraph
{
	GENERATED_BODY()

public:
	friend class UAnimNextStateTree_EditorData;
	friend class UAnimNextStateTreeFactory;

	UPROPERTY()
	TObjectPtr<UStateTree> StateTree;
};