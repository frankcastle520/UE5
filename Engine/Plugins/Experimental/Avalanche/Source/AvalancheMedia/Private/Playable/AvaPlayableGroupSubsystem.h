// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"

#include "AvaPlayableGroupSubsystem.generated.h"

class UAvaPlayableGroupManager;

UCLASS()
class UAvaPlayableGroupSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UGameInstanceSubsystem

	UPROPERTY()
	TObjectPtr<UAvaPlayableGroupManager> PlayableGroupManager;
};
