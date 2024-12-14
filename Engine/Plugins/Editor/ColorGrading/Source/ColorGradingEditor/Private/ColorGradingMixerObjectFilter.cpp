// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorGradingMixerObjectFilter.h"
#include "ColorGradingMixerObjectFilterRegistry.h"

TSet<UClass*> UColorGradingMixerObjectFilter::GetObjectClassesToFilter() const
{
	return FColorGradingMixerObjectFilterRegistry::GetObjectClassesToFilter();
}

TSet<TSubclassOf<AActor>> UColorGradingMixerObjectFilter::GetObjectClassesToPlace() const
{
	return FColorGradingMixerObjectFilterRegistry::GetActorClassesToPlace();
}