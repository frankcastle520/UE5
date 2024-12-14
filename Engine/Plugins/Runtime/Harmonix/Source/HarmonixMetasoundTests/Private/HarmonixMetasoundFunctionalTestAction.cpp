// Copyright Epic Games, Inc. All Rights Reserved.

#include "HarmonixMetasoundFunctionalTestAction.h"
#include "Components/AudioComponent.h"

#include "FunctionalTest.h"

void UHarmonixMetasoundFunctionalTestActionSetAudioParameter::OnStart_Implementation(AFunctionalTest* Test)
{
	if (Test)
	{
		if (TObjectPtr<UAudioComponent> AudioComponent = Test->FindComponentByClass<UAudioComponent>())
		{
			AudioComponent->SetParameter(FAudioParameter(AudioParameter));
			Finish(true);
		}
		else
		{
			Finish(false);
		}
	}
}

void UHarmonixMetasoundFunctionalTestActionSetWaitForAudioFinished::OnStart_Implementation(AFunctionalTest* Test)
{
	if (Test)
	{
		if (TObjectPtr<UAudioComponent> AudioComponent = Test->FindComponentByClass<UAudioComponent>())
		{
			AudioComponent->OnAudioFinished.AddDynamic(this, &UHarmonixMetasoundFunctionalTestActionSetWaitForAudioFinished::OnAudioFinished);
		}
	}
}

void UHarmonixMetasoundFunctionalTestActionSetWaitForAudioFinished::OnAudioFinished()
{
	Finish(true);
}
