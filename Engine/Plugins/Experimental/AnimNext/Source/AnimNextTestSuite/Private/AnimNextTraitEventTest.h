// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TraitCore/TraitUID.h"
#include "TraitCore/TraitEvent.h"

struct FTraitCoreTest_EventA : public FAnimNextTraitEvent
{
	DECLARE_ANIM_TRAIT_EVENT(FTraitCoreTest_EventA, FAnimNextTraitEvent)

	bool bAlwaysForwardToBase = true;

	TArray<UE::AnimNext::FTraitUID> VisitedTraits;
};

struct FTraitCoreTest_EventB : public FAnimNextTraitEvent
{
	DECLARE_ANIM_TRAIT_EVENT(FTraitCoreTest_EventB, FAnimNextTraitEvent)

	TArray<UE::AnimNext::FTraitUID> VisitedTraits;
};
