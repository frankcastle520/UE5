// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneSpawnRegister.h"
#include "UObject/Class.h"

class IMovieScenePlayer;
class IMovieSceneObjectSpawner;
class UMovieSceneSpawnableBindingBase;

/** Movie scene spawn register that knows how to handle spawning objects (actors) for a DaySequence  */
class DAYSEQUENCE_API FDaySequenceSpawnRegister : public FMovieSceneSpawnRegister
{
public:
	FDaySequenceSpawnRegister();

protected:
	/** ~ FMovieSceneSpawnRegister interface */
	virtual UObject* SpawnObject(FMovieSceneSpawnable& Spawnable, FMovieSceneSequenceIDRef TemplateID, TSharedRef<const FSharedPlaybackState> SharedPlaybackState) override;
	virtual void DestroySpawnedObject(UObject& Object, UMovieSceneSpawnableBindingBase* CustomSpawnableBinding) override;

#if WITH_EDITOR
	virtual bool CanSpawnObject(UClass* InClass) const override;
#endif

protected:
	/** Extension object spawners */
	TArray<TSharedRef<IMovieSceneObjectSpawner>> MovieSceneObjectSpawners;
};
