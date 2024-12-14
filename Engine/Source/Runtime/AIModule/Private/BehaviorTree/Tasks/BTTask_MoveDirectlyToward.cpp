// Copyright Epic Games, Inc. All Rights Reserved.

#include "BehaviorTree/Tasks/BTTask_MoveDirectlyToward.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BTTask_MoveDirectlyToward)

UBTTask_MoveDirectlyToward::UBTTask_MoveDirectlyToward(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NodeName = "MoveDirectlyToward";
	bUsePathfinding = false;
}

#if WITH_EDITOR

FName UBTTask_MoveDirectlyToward::GetNodeIconName() const
{
	return FName("BTEditor.Graph.BTNode.Task.MoveDirectlyToward.Icon");
}

#endif	// WITH_EDITOR

