// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDDataContainerBaseActor.h"
#include "Components/ChaosVDSolverDataComponent.h"

AChaosVDDataContainerBaseActor::AChaosVDDataContainerBaseActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AChaosVDDataContainerBaseActor::UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData)
{
	TInlineComponentArray<UChaosVDSolverDataComponent*> SolverDataComponents(this);
	for (UChaosVDSolverDataComponent* Component : SolverDataComponents)
	{
		if (Component)
		{
			Component->UpdateFromNewGameFrameData(InGameFrameData);
		}
	}
}

void AChaosVDDataContainerBaseActor::Destroyed()
{
	TInlineComponentArray<UChaosVDSolverDataComponent*> SolverDataComponents(this);
	for (UChaosVDSolverDataComponent* Component : SolverDataComponents)
	{
		if (Component)
		{
			Component->ClearData();
		}
	}

	Super::Destroyed();
}

