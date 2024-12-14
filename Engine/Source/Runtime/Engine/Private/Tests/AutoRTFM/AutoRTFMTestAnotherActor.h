// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "AutoRTFMTestAnotherActor.generated.h"

UCLASS()
class AAutoRTFMTestAnotherActor : public AActor
{
    GENERATED_BODY()

public:
    int Value = 42;
};
