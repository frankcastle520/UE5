// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsEngine/BodySetup.h"
#include "AutoRTFMTestBodySetup.generated.h"

UCLASS()
class UAutoRTFMTestBodySetup : public UBodySetup
{
    GENERATED_BODY()

public:
    int Value = 42;
};
