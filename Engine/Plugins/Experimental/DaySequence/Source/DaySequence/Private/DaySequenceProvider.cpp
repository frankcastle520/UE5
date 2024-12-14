// Copyright Epic Games, Inc. All Rights Reserved.

#include "DaySequenceProvider.h"
#include "DaySequenceActor.h"
#include "DaySequenceSubsystem.h"

#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DaySequenceProvider)

ADaySequenceProvider::ADaySequenceProvider(const FObjectInitializer& Init)
: Super(Init)
{
	USceneComponent* SceneRootComponent = CreateDefaultSubobject<USceneComponent>(USceneComponent::GetDefaultSceneRootVariableName());
	SetRootComponent(SceneRootComponent);
}

TArrayView<TObjectPtr<UDaySequence>> ADaySequenceProvider::GetDaySequences()
{
	return DaySequenceAssets;
}

#if WITH_EDITOR
void ADaySequenceProvider::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ADaySequenceProvider, DaySequenceAssets))
	{
		// Force update the DaySequenceActor's sequence.
		// TODO: only update if this actor is assigned to the DayActor.
		if (const UDaySequenceSubsystem* DaySubsystem = GetWorld()->GetSubsystem<UDaySequenceSubsystem>())
		{
			if (ADaySequenceActor* DayActor = DaySubsystem->GetDaySequenceActor())
			{
				DayActor->UpdateRootSequence();
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif




