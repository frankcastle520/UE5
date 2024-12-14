// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneAnimatorSection.h"

UMovieSceneAnimatorSection::UMovieSceneAnimatorSection()
	: UMovieSceneSection()
{
	bSupportsInfiniteRange = false;
	EvalOptions.CompletionMode = EMovieSceneCompletionMode::RestoreState;
}

void UMovieSceneAnimatorSection::SetEvalTimeMode(EMovieSceneAnimatorEvalTimeMode InMode)
{
	EvalTimeMode = InMode;
}

void UMovieSceneAnimatorSection::SetCustomStartTime(double InTime)
{
	CustomStartTime = InTime;
}

void UMovieSceneAnimatorSection::SetCustomEndTime(double InTime)
{
	CustomEndTime = InTime;
}
