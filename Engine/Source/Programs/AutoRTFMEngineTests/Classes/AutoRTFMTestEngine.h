// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "AutoRTFMTestEngine.generated.h"

UCLASS()
class UAutoRTFMTestEngine : public UEngine
{
    GENERATED_BODY()

public:
    int Value = 42;

	void Tick(float, bool) override {}
};
