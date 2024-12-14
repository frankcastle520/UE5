// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ProceduralDaySequenceBuilder.h"

#include "GameFramework/Actor.h"

#include "ProceduralDaySequence.generated.h"

class ADaySequenceActor;
class UDaySequence;

namespace UE::DaySequence
{
	// Utility function to simplify looking for owned components by type and name.
	template <typename T>
	T* GetComponentByName(AActor* InActor, FName Name)
	{
		for (T* Component : TInlineComponentArray<T*>(InActor))
		{
			if (Component->GetFName() == Name)
			{
				return Component;
			}
		}
		
		return nullptr;
	}
}

/**
 * Base class for procedural sequences.
 * To create a procedural sequence, a subclass of this type should be created that overrides BuildSequence.
 * See FSunPositionSequence, FSunAngleSequence, and FSineSequence for examples.
 */
USTRUCT(meta=(Hidden))
struct DAYSEQUENCE_API FProceduralDaySequence
{
	GENERATED_BODY()
	
	virtual ~FProceduralDaySequence()
	{}
	
	UDaySequence* GetSequence(ADaySequenceActor* InActor);

protected:
	
	virtual void BuildSequence(UProceduralDaySequenceBuilder* InBuilder) {}
	
	TWeakObjectPtr<ADaySequenceActor> WeakTargetActor = nullptr;
};