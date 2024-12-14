// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeSources/PropertyAnimatorCoreWorldTimeSource.h"

#include "Engine/World.h"

bool UPropertyAnimatorCoreWorldTimeSource::UpdateEvaluationData(FPropertyAnimatorCoreTimeSourceEvaluationData& OutData)
{
	const UWorld* World = GetWorld();

	if (!World)
	{
		return false;
	}

	OutData.TimeElapsed = World->GetTimeSeconds();

	return World->IsEditorWorld()
		|| World->IsPreviewWorld()
		|| (World->IsGameWorld() && World->HasBegunPlay());
}
