// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDDataContainerBaseActor.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectPtr.h"

#include "ChaosVDGameFrameInfoActor.generated.h"

class UChaosVDGenericDebugDrawDataComponent;
class UChaosVDSceneQueryDataComponent;

/** Actor that contains game frame related data */
UCLASS(NotBlueprintable, NotPlaceable)
class AChaosVDGameFrameInfoActor : public AChaosVDDataContainerBaseActor
{
	GENERATED_BODY()

public:
	AChaosVDGameFrameInfoActor();

	UChaosVDGenericDebugDrawDataComponent* GetGenericDebugDrawDataComponent() const { return GenericDebugDrawDataComponent.Get(); }

	void CleanUp();

	UPROPERTY()
	TObjectPtr<UChaosVDGenericDebugDrawDataComponent> GenericDebugDrawDataComponent;
};
