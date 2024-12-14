// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "EngineRuntimeTests.generated.h"

class UBillboardComponent;

/** A simple actor class that can be manually ticked to test for correctness and performance */
UCLASS()
class RUNTIMETESTS_API AEngineTestTickActor : public AActor
{
	GENERATED_BODY()

public:
	AEngineTestTickActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Number of times this has ticked since reset */
	UPROPERTY(BlueprintReadOnly, Category=Default)
	int32 TickCount;
	
	/** Indicates when this was ticked in a frame, with 1 being first */
	UPROPERTY(BlueprintReadOnly, Category = Default)
	int32 TickOrder;

	/** Used to set TickOrder, reset to 1 at the start of every frame */
	static int32 CurrentTickOrder;

	/** If it should actually increase tick count */
	UPROPERTY(BlueprintReadOnly, Category = Default)
	bool bShouldIncrementTickCount;

	/** If it should perform other busy work */
	UPROPERTY(BlueprintReadOnly, Category=Default)
	bool bShouldDoMath;

	/** Used for bShouldDoMath */
	UPROPERTY(BlueprintReadOnly, Category=Default)
	float MathCounter;

	/** Used for bShouldDoMath */
	UPROPERTY(BlueprintReadOnly, Category=Default)
	float MathIncrement;

	/** Used for bShouldDoMath */
	UPROPERTY(BlueprintReadOnly, Category=Default)
	float MathLimit;

	/** Reset state before next test, call this after unregistering tick */
	virtual void ResetState();

	/** Do the actual work */
	void DoTick();

	/** Virtual function wrapper */
	virtual void VirtualTick();

	/** AActor version */
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;
};

#if WITH_AUTOMATION_WORKER

/** Automation test base class that wraps a test world and handles checking tick counts */
class RUNTIMETESTS_API FEngineTickTestBase : public FAutomationTestBase
{
public:
	FEngineTickTestBase(const FString& InName, const bool bInComplexTask);

	virtual ~FEngineTickTestBase();

	/** Gets the world being tested */
	UWorld* GetTestWorld() const;

	/** Creates a world where actors can be spawned */
	virtual bool CreateTestWorld();

	/** Spawn actors of subclass */
	virtual bool CreateTestActors(int32 ActorCount, TSubclassOf<AEngineTestTickActor> ActorClass);

	/** Start play in world, prepare for ticking */
	virtual bool BeginPlayInTestWorld();

	/** Tick one frame in test world */
	virtual bool TickTestWorld(float DeltaTime = 0.01f);

	/** Reset the test */
	virtual bool ResetTestActors();

	/** Checks TickCount on every actor */
	virtual bool CheckTickCount(const TCHAR* TickTestName, int32 TickCount);

	/** Destroys the test actors */
	virtual bool DestroyAllTestActors();
	
	/** Destroys the test world */
	virtual bool DestroyTestWorld();

	/** Reports errors to automation system, returns true if there were errors */
	virtual bool ReportAnyErrors();

protected:
	TUniquePtr<FTestWorldWrapper> WorldWrapper;
	TArray<AEngineTestTickActor*> TestActors;
};

#endif // WITH_AUTOMATION_WORKER