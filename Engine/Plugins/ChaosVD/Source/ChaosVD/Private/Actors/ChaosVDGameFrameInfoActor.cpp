// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/ChaosVDGameFrameInfoActor.h"

#include "Components/ChaosVDGenericDebugDrawDataComponent.h"
#include "Components/ChaosVDSceneQueryDataComponent.h"

AChaosVDGameFrameInfoActor::AChaosVDGameFrameInfoActor()
{
	PrimaryActorTick.bCanEverTick = false;

	GenericDebugDrawDataComponent = CreateDefaultSubobject<UChaosVDGenericDebugDrawDataComponent>(TEXT("UChaosVDGenericDebugDrawDataComponent"));

	constexpr int32 GenericGameFrameDataSolverID = INDEX_NONE;
	GenericDebugDrawDataComponent->SetSolverID(GenericGameFrameDataSolverID);
}

void AChaosVDGameFrameInfoActor::CleanUp()
{
	if (GenericDebugDrawDataComponent)
	{
		GenericDebugDrawDataComponent->ClearData();
	}
}
