// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_ConfigureInstancedActors.h"
#include "InstancedActorsSettings.h"
#include "InstancedActorsSubsystem.h"
#include "GameFeaturesSubsystem.h"


void UGameFeatureAction_ConfigureInstancedActors::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);

	GetMutableDefault<UInstancedActorsProjectSettings>()->RegisterConfigOverride(*this, ConfigOverride);
}

void UGameFeatureAction_ConfigureInstancedActors::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	GetMutableDefault<UInstancedActorsProjectSettings>()->UnregisterConfigOverride(*this);

	Super::OnGameFeatureDeactivating(Context);
}
