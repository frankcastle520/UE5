// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProceduralMeshes/SVGBaseDynamicMeshComponent.h"
#include "SVGEngineSubsystem.h"

USVGBaseDynamicMeshComponent::USVGBaseDynamicMeshComponent()
{
	bMeshHasBeenUpdated = false;
}

void USVGBaseDynamicMeshComponent::OnRegister()
{
	Super::OnRegister();

	if (bMeshHasBeenUpdated)
	{
		if (AActor* Owner = GetOwner())
		{
			USVGEngineSubsystem::OnSVGShapesUpdated().Execute(Owner);
		}

		bMeshHasBeenUpdated = false;
	}
}

void USVGBaseDynamicMeshComponent::MarkSVGMeshUpdated()
{
	bMeshHasBeenUpdated = true;
}
