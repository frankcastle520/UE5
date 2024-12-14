// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ChaosVDSolverDataComponent.h"


void UChaosVDSolverDataComponent::SetScene(const TWeakPtr<FChaosVDScene>& InSceneWeakPtr)
{
	SceneWeakPtr = InSceneWeakPtr;
}
