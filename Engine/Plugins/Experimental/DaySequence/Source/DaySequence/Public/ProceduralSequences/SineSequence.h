// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProceduralDaySequence.h"

#include "SineSequence.generated.h"

/**
 * A procedural sequence that animates a user specified property according to a sine wave.
 */
USTRUCT()
struct DAYSEQUENCE_API FSineSequence : public FProceduralDaySequence
{
	GENERATED_BODY()

	virtual ~FSineSequence() override
	{}
	
	// The name of the float property to animate.
	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	FName PropertyName = FName(TEXT(""));
	
	// Optional component, used if the property being animated is on a component owned by the Day Sequence Actor.
	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	FName ComponentName = FName(TEXT(""));

	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	unsigned KeyCount = 5;

	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	float Amplitude = 1.f;
	
	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	float Frequency = 1.f;
	
	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	float PhaseShift = 0.f;

	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	float VerticalShift = 0.f;
	
private:
	
	virtual void BuildSequence(UProceduralDaySequenceBuilder* InBuilder) override;
};