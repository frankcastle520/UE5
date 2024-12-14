// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/DeveloperSettings.h"

#include "SurfaceEffectsSettings.generated.h"

UCLASS(config=Engine, defaultconfig, meta=(DisplayName="Surface Effects"))
class SURFACEEFFECTS_API USurfaceEffectsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Data table for storing surface effect rules @see FSurfaceEffectTableRow
	 */
	UPROPERTY(config, EditAnywhere, Category = GameplayTags, meta = (AllowedClasses = "/Script/Engine.DataTable", RowType = "/Script/SurfaceEffects.SurfaceEffectTableRow"))
	FSoftObjectPath SurfaceEffectsDataTable;
};