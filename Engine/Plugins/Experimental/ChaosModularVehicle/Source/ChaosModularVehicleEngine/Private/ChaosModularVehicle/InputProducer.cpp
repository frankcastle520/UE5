// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosModularVehicle/InputProducer.h"

// Default input producer

void UVehicleDefaultInputProducer::InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut)
{
	MergedInput.Initialize(SetupData, NameMapOut);
}

void UVehicleDefaultInputProducer::BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue)
{
	//UE_LOG(LogTemp, Warning, TEXT("BufferInput - InName %s Val=%s"), *InName.ToString(), *InValue.ToString());

	// inputs are merged here rather than buffered, since they would be merged anyway before use in ProduceInput
	FInputInterface Inputs(InNameMap, MergedInput);
	Inputs.MergeValue(InName, InValue);
}

void UVehicleDefaultInputProducer::ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer)
{
	//UE_LOG(LogTemp, Warning, TEXT("ProduceInput - PhysicsStep %d, NumSteps %d"), PhysicsStep, NumSteps);
	//FInputInterface Inputs(InNameMap, MergedInput);
	//UE_LOG(LogTemp, Warning, TEXT(".. Throttle %s"), *Inputs.GetValue("Throttle").ToString());

	// copy state out
	InOutContainer = MergedInput;

	// reset state for next frame
	MergedInput.ZeroValues();
}


// Example Playback Input Producer

void UVehiclePlaybackInputProducer::InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut)
{
	int32 Seed = 123;
	FRandomStream Random(Seed);
	StartStep = 0;

	// Initialize a single container once
	FModuleInputContainer InputsForFrame;
	InputsForFrame.Initialize(SetupData, NameMapOut);

	PlaybackBuffer.Reserve(BufferLength);
	for (int32 I = 0; I < BufferLength; I++)
	{
		// copy initialized container into array
		PlaybackBuffer.Emplace(InputsForFrame);

		// change value of some intputs
		FInputInterface Inputs(NameMapOut, PlaybackBuffer[I]);
		Inputs.SetValue("Throttle", Random.FRand());
		Inputs.SetValue("Steering", 1.0f - 2.0f * Random.FRand());
	}

}

void UVehiclePlaybackInputProducer::BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue)
{
	// NOP
}

void UVehiclePlaybackInputProducer::ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer)
{
	if (StartStep == 0)
	{
		StartStep = PhysicsStep;
	}

	int32 UseIndex = PhysicsStep - StartStep;
	if (UseIndex >=0 && UseIndex <= BufferLength)
	{
		FModuleInputContainer& UseContainer = PlaybackBuffer[PhysicsStep - StartStep];
		InOutContainer = UseContainer;
	}
	else
	{
		InOutContainer.ZeroValues();
		StartStep = PhysicsStep;	// restart loop
	}

}


// Example Random Input Producer

void UVehicleRandomInputProducer::InitializeContainer(TArray<FModuleInputSetup>& SetupData, FInputNameMap& NameMapOut)
{
	PlaybackContainer.Initialize(SetupData, NameMapOut);
}

void UVehicleRandomInputProducer::BufferInput(const FInputNameMap& InNameMap, const FName InName, const FModuleInputValue& InValue)
{
	// NOP
}

void UVehicleRandomInputProducer::ProduceInput(int32 PhysicsStep, int32 NumSteps, const FInputNameMap& InNameMap, FModuleInputContainer& InOutContainer)
{
	int32 Seed = 123;
	static FRandomStream Random(Seed);

	// new control settings generated every ChangeInputFrequency number of frames (every frame is too quick)
	// previous controls are held in the PlaybackContainer between the changes
	if (PhysicsStep % ChangeInputFrequency == 0)
	{
		// clear old input
		PlaybackContainer.ZeroValues();

		// generate new random input
		FInputInterface Inputs(InNameMap, PlaybackContainer);
		Inputs.SetValue("Throttle", Random.FRand());
		Inputs.SetValue("Steering", 1.0f - 2.0f * Random.FRand());
	}

	InOutContainer = PlaybackContainer;

}

