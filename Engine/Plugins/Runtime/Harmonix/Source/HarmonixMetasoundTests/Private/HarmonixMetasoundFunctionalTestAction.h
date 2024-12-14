// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HarmonixFunctionalTestAction.h"
#include "AudioParameter.h"

#include "HarmonixMetasoundFunctionalTestAction.generated.h"

UCLASS(NotBlueprintable, Meta=(DisplayName="Set Audio Parameter"))
class UHarmonixMetasoundFunctionalTestActionSetAudioParameter : public UHarmonixFunctionalTestAction
{
	GENERATED_BODY()
public:
	virtual void OnStart_Implementation(AFunctionalTest* Test) override;

	UPROPERTY(EditAnywhere, Category = "Functional Testing")
	FAudioParameter AudioParameter;
};

UCLASS(NotBlueprintable, Meta=(DisplayName="Wait For Audio Finished"))
class UHarmonixMetasoundFunctionalTestActionSetWaitForAudioFinished : public UHarmonixFunctionalTestAction
{
	GENERATED_BODY()
public:
	virtual void OnStart_Implementation(AFunctionalTest* Test) override;
protected:
	UFUNCTION()
	void OnAudioFinished();
};