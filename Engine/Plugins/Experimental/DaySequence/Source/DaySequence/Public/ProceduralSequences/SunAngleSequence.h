// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProceduralDaySequence.h"

#include "SunAngleSequence.generated.h"

/**
 * A procedural sequence that linearly animates the sun.
 */
USTRUCT()
struct DAYSEQUENCE_API FSunAngleSequence : public FProceduralDaySequence
{
	GENERATED_BODY()

	virtual ~FSunAngleSequence() override
	{}

	UPROPERTY(EditAnywhere, Category = "Procedural Parameters")
	FName SunComponentName = FName(TEXT("Sun"));

private:
	
	virtual void BuildSequence(UProceduralDaySequenceBuilder* InBuilder) override;
};