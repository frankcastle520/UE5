// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "AutoRTFMTestPrimitiveComponent.generated.h"

UCLASS()
class UAutoRTFMTestPrimitiveComponent : public UPrimitiveComponent
{
    GENERATED_BODY()

public:
    int Value = 42;

	UBodySetup* BodySetup = nullptr;

	UBodySetup* GetBodySetup() override { return BodySetup; }
};
