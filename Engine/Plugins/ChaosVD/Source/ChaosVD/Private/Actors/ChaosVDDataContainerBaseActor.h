// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "ChaosVDDataContainerBaseActor.generated.h"

class FChaosVDScene;

struct FChaosVDSolverFrameData;
struct FChaosVDGameFrameData;

/** Base class for any CVD actor that will contain frame related data (either solver frame or game frame) */
UCLASS(Abstract, NotBlueprintable, NotPlaceable)
class CHAOSVD_API AChaosVDDataContainerBaseActor : public AActor
{
	GENERATED_BODY()

public:
	AChaosVDDataContainerBaseActor();
	
	virtual void UpdateFromNewGameFrameData(const FChaosVDGameFrameData& InGameFrameData);

	virtual void UpdateFromNewSolverFrameData(const FChaosVDSolverFrameData& InSolverFrameData) {};

	virtual void Destroyed() override;

	virtual void SetScene(TWeakPtr<FChaosVDScene> InScene) { SceneWeakPtr = InScene; }

	virtual bool IsVisible() const { return true;}
	
	TWeakPtr<FChaosVDScene> GetScene() const { return SceneWeakPtr; }
	
protected:
	TWeakPtr<FChaosVDScene> SceneWeakPtr;
};
