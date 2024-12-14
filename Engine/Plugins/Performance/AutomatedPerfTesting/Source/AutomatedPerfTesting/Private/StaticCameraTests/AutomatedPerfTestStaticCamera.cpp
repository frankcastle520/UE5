// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticCameraTests/AutomatedPerfTestStaticCamera.h"

AAutomatedPerfTestStaticCamera::AAutomatedPerfTestStaticCamera(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	// disable tick on these actors, as it's not needed
	AActor::SetActorTickEnabled(false);
}
