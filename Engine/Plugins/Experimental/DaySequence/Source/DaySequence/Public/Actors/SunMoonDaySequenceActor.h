// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseDaySequenceActor.h"

#include "SunMoonDaySequenceActor.generated.h"

/**
 * A Day Sequence Actor that represents a physically accurate 24 hour day cycle.
 */
UCLASS(Blueprintable)
class DAYSEQUENCE_API ASunMoonDaySequenceActor
	: public ABaseDaySequenceActor
{
	GENERATED_BODY()

public:
	ASunMoonDaySequenceActor(const FObjectInitializer& Init);

protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category= "Day Sequence", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDirectionalLightComponent> MoonComponent;
};