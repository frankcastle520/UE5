// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "UObject/ObjectPtr.h"

#include "ChaosVDGeometryContainer.generated.h"

/** Actor that contains Static Mesh Components used to visualize the geometry we generated from the recorded data */
UCLASS()
class CHAOSVD_API AChaosVDGeometryContainer : public AActor
{
	GENERATED_BODY()

public:
	AChaosVDGeometryContainer();

	void CleanUp();
};