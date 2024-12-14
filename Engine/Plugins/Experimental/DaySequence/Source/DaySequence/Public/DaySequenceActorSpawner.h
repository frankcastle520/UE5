// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieSceneObjectSpawner.h"

class DAYSEQUENCE_API FDaySequenceActorSpawner : public IMovieSceneObjectSpawner
{
public:
	static TSharedRef<IMovieSceneObjectSpawner> CreateObjectSpawner();

	// IMovieSceneObjectSpawner interface
	virtual UClass* GetSupportedTemplateType() const override;
	virtual UObject* SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, TSharedRef<const UE::MovieScene::FSharedPlaybackState> SharedPlaybackState) override;
	virtual void DestroySpawnedObject(UObject& Object) override;
};
