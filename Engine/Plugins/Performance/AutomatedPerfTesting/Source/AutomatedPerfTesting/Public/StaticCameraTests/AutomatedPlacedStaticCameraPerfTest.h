// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "StaticCameraTests/AutomatedStaticCameraPerfTestBase.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "AutomatedPlacedStaticCameraPerfTest.generated.h"

UCLASS()
class AUTOMATEDPERFTESTING_API UAutomatedPlacedStaticCameraPerfTest : public UAutomatedStaticCameraPerfTestBase
{
	GENERATED_BODY()

public:
	virtual TArray<ACameraActor*> GetMapCameraActors() override;
};