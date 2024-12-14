// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Camera/CameraActor.h"
#include "AutomatedPerfTestStaticCamera.generated.h"

UCLASS(Blueprintable, BlueprintType, ClassGroup=Performance)
class AUTOMATEDPERFTESTING_API AAutomatedPerfTestStaticCamera : public ACameraActor
{
	GENERATED_BODY()

	AAutomatedPerfTestStaticCamera(const FObjectInitializer& ObjectInitializer);
};