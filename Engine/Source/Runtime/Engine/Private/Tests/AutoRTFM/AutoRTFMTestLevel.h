// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Level.h"
#include "AutoRTFMTestLevel.generated.h"

UCLASS()
class UAutoRTFMTestLevel : public ULevel
{
    GENERATED_BODY()

public:
    int Value = 42;
};
