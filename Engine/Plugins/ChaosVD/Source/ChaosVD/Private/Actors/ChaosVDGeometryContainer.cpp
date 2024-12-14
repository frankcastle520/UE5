// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDGeometryContainer.h"
#include "Components/MeshComponent.h"
#include "Containers/Array.h"
#include "Misc/MemStack.h"
#include "Misc/ScopedSlowTask.h"

AChaosVDGeometryContainer::AChaosVDGeometryContainer()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AChaosVDGeometryContainer::CleanUp()
{
	// We can have ten of thousands of components, so the InlineComponents arrays will not be better than a normal array.
	FMemMark Mark(FMemStack::Get());
	TArray<UMeshComponent*, TMemStackAllocator<>> ComponentsToDestroy;

	GetComponents<UMeshComponent>(ComponentsToDestroy);

	constexpr float AmountOfWork = 1.0f;
	const float PercentagePerElement = 1.0f / ComponentsToDestroy.Num();

	FScopedSlowTask CleaningDataSlowTask(AmountOfWork, NSLOCTEXT("ChaosVisualDebugger", "CleaningupGeometryData", "Clearing Geoemtry Data ..."));
	CleaningDataSlowTask.MakeDialog();

	for (UMeshComponent* Component : ComponentsToDestroy)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}

		CleaningDataSlowTask.EnterProgressFrame(PercentagePerElement);
	}
}
