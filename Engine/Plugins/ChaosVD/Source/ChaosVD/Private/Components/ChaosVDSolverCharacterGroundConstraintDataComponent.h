// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDConstraintDataComponent.h"
#include "ChaosVDSolverCharacterGroundConstraintDataComponent.generated.h"

UCLASS()
class CHAOSVD_API UChaosVDSolverCharacterGroundConstraintDataComponent : public UChaosVDConstraintDataComponent
{
	GENERATED_BODY()
public:
	virtual void SetScene(const TWeakPtr<FChaosVDScene>& InSceneWeakPtr) override;

	void HandleSceneUpdated();

	virtual void BeginDestroy() override;
};
