// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "AutoRTFMTestActor.generated.h"

UCLASS()
class AAutoRTFMTestActor : public AActor
{
    GENERATED_BODY()

public:
    int Value = 42;
};
