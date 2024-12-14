// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ShallowWaterSubsystem.h"
#include "BasicShallowWaterSubsystem.generated.h"


UCLASS(Blueprintable, Transient)
class WATERADVANCED_API UBasicShallowWaterSubsystem : public UShallowWaterSubsystem
{
	GENERATED_BODY()

public:
	UBasicShallowWaterSubsystem();

	virtual bool IsShallowWaterAllowedToInitialize() const override;	
};
