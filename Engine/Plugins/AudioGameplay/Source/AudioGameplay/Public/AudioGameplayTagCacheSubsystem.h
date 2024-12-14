// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "MapAnyKey.h"
#include "Subsystems/WorldSubsystem.h"

#include "AudioGameplayTagCacheSubsystem.generated.h"

using FGameplayTagMap = UE::AudioGameplay::TMapAnyKey<FGameplayTag>;

/**
 * UAudioGameplayTagCacheSubsystem - Per world subsystem used to persist gameplay tags that are expensive to construct dynamically from parts
 */
UCLASS()
class AUDIOGAMEPLAY_API UAudioGameplayTagCacheSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public: 

	UAudioGameplayTagCacheSubsystem() = default;
	virtual ~UAudioGameplayTagCacheSubsystem() = default;

	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	static UAudioGameplayTagCacheSubsystem* Get(const UWorld* WorldContext);

	FGameplayTagMap& GetTagCache()
	{
		return GameplayTagCache;
	}

protected:

	FGameplayTagMap GameplayTagCache;
};

