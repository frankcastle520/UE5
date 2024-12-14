// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Systems/MovieScenePropertySystem.h"
#include "MovieSceneRotatorPropertySystem.generated.h"

/** System for rotator property registered within PropertyRegistry */
UCLASS(MinimalAPI)
class UMovieSceneRotatorPropertySystem : public UMovieScenePropertySystem
{
	GENERATED_BODY()

public:
	UMovieSceneRotatorPropertySystem(const FObjectInitializer& InObjectInitializer);

	//~ Begin UMovieSceneEntitySystem
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& InSubsequents) override;
	//~ End UMovieSceneEntitySystem
};
