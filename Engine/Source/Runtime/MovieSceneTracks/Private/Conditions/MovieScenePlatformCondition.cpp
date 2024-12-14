// Copyright Epic Games, Inc. All Rights Reserved.

#include "Conditions/MovieScenePlatformCondition.h"
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "EntitySystem/MovieSceneSharedPlaybackState.h"
#include "Kismet/GameplayStatics.h"

bool UMovieScenePlatformCondition::EvaluateConditionInternal(FGuid BindingGuid, FMovieSceneSequenceID SequenceID, TSharedRef<const UE::MovieScene::FSharedPlaybackState> SharedPlaybackState) const
{
	FString PlatformName = UGameplayStatics::GetPlatformName();
	return ValidPlatforms.Contains(*PlatformName);
}
