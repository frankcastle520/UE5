// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "ChaosVDSolverDataComponent.generated.h"

class FChaosVDScene;
struct FChaosVDGameFrameData;
/**
 * Base class for all components that stores recorded solver data
 */
UCLASS(Abstract)
class UChaosVDSolverDataComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual void ClearData() PURE_VIRTUAL(UChaosVDSolverDataComponent::ClearData);
	virtual void SetScene(const TWeakPtr<FChaosVDScene>& InSceneWeakPtr);

	virtual void UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData) {};
	
	void SetSolverID(int32 InSolverID) { SolverID = InSolverID; }

protected:

	TWeakPtr<FChaosVDScene> SceneWeakPtr;

	int32 SolverID = INDEX_NONE;
};

