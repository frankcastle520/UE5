// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "HAL/Platform.h"
#include "Templates/SubclassOf.h"

class UClass;

/**
 * Contains functions for controlling which classes can be managed from the Color Grading panel's ObjectMixer-based hierarchy panel
 */
struct COLORGRADINGEDITOR_API FColorGradingMixerObjectFilterRegistry
{
public:

	/** Register an object class that can be seen in a Color Grading panel's object list */
	static void RegisterObjectClassToFilter(UClass* Class)
	{
		ObjectClassesToFilter.Add(Class);
	}

	/** Register an actor class that can be placed from the Color Grading panel's object list */
	static void RegisterActorClassToPlace(TSubclassOf<AActor> Class)
	{
		ActorClassesToPlace.Add(Class);
	}

	/** Get the set of object classes that can be seen in a Color Grading panel's object list */
	static const TSet<UClass*>& GetObjectClassesToFilter() { return ObjectClassesToFilter; }

	/** Get the set of actor classes that can be placed from a Color Grading panel's object list */
	static const TSet<TSubclassOf<AActor>>& GetActorClassesToPlace() { return ActorClassesToPlace; }

private:
	/** Set of classes that can be seen in the object panel */
	inline static TSet<UClass*> ObjectClassesToFilter;

	/** Set of classes that can be placed from the object panel */
	inline static TSet<TSubclassOf<AActor>> ActorClassesToPlace;
};
