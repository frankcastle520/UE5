// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticCameraTests/AutomatedPlacedStaticCameraPerfTest.h"
#include "Kismet/GameplayStatics.h"
#include "StaticCameraTests/AutomatedPerfTestStaticCamera.h"

TArray<ACameraActor*> UAutomatedPlacedStaticCameraPerfTest::GetMapCameraActors()
{
	TArray<AActor*> FoundCameras;
	TArray<ACameraActor*> OutCameras;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AAutomatedPerfTestStaticCamera::StaticClass(), FoundCameras);

	for(AActor* Actor : FoundCameras)
	{
		if(ACameraActor* FoundCamera = Cast<ACameraActor>(Actor))
		{
			OutCameras.Add(FoundCamera);	
		}
	}

	return OutCameras;
}
