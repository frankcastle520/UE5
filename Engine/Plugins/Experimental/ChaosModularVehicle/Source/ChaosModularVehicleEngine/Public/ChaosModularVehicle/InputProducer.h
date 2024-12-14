// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SimModule/ModuleInput.h"
#include "InputProducer.generated.h"

/** The default input producer that takes real input from the player and provides it to the simulation */
UCLASS(BlueprintType, Blueprintable)
class CHAOSMODULARVEHICLEENGINE_API UVehicleDefaultInputProducer : public UVehicleInputProducerBase
{
	GENERATED_BODY()

public:

	/* initialize the input buffer container */
	virtual void InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut) override;

	/** capture input at game thread frequency */
	virtual void BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue) override;

	/** produce input for PT simulation at PT frequency */
	virtual void ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer) override;

	FModuleInputContainer MergedInput;
};


/** Example input generator, generates random input inot a per frame buffer then replays from the buffer, looping back to the start when the bugger is exhausted */
UCLASS(BlueprintType, Blueprintable)
class CHAOSMODULARVEHICLEENGINE_API UVehiclePlaybackInputProducer : public UVehicleInputProducerBase
{
	GENERATED_BODY()

public:

	/* initialize the input buffer containers */
	virtual void InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut) override;

	/** capture input at game thread frequency */
	virtual void BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue) override;

	/** produce input for PT simulation at PT frequency */
	virtual void ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer) override;

	TArray<FModuleInputContainer> PlaybackBuffer;
	int32 BufferLength = 150;
	int32 StartStep = 0;
};


/** Example input generator, generates random input on the fly for the PT */
UCLASS(BlueprintType, Blueprintable)
class CHAOSMODULARVEHICLEENGINE_API UVehicleRandomInputProducer : public UVehicleInputProducerBase
{
	GENERATED_BODY()

public:

	/* initialize the input buffer containers */
	virtual void InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut) override;

	/** capture input at game thread frequency */
	virtual void BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue) override;

	/** produce input for PT simulation at PT frequency */
	virtual void ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer) override;

	FModuleInputContainer PlaybackContainer;
	int32 ChangeInputFrequency = 10;
};