// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SpawnHelper.h"

/*
//Example boiler plate

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

TEST_CLASS(MyFixtureName, "ActorSpawner.Example")
{
	FActorTestSpawner Spawner;

	TEST_METHOD(Spawn_BaseActor_DefaultActorSpawned)
	{
		AActor& Actor = Spawner.SpawnActor<AActor>();
		ASSERT_THAT(IsTrue(Actor.GetFName() == NAME_None));
	}

	TEST_METHOD(Spawn_BaseObject_DefaultObjectSpawned)
	{
		UObject& Object = Spawner.SpawnObject<UObject>();
		ASSERT_THAT(IsTrue(Object.GetFName() == NAME_None));
	}
};
*/

class UTestGameInstance;

/** Class for spawning actors in an ActorTest context(no PIE loaded) */
struct CQTEST_API FActorTestSpawner : public FSpawnHelper
{
	FActorTestSpawner() = default;

	virtual ~FActorTestSpawner() override;

	/** Initialize the GameSubsystem for the test world. */
	void InitializeGameSubsystems();

	/** Returns a pointer to the test game instance. */
	UTestGameInstance* GetGameInstance();

protected:
	/** Returns a newly created world. */
	virtual UWorld* CreateWorld() override;

private:
	UTestGameInstance* GameInstance{ nullptr };
};