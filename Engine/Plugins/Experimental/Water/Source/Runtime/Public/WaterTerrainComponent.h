// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WaterTerrainComponent.generated.h"

class AWaterZone;

/**
 * Water Terrain Component can be attached to any actor with primitive components to allow them to render into a Water Info Texture as the terrain.
 */
UCLASS(meta=(BlueprintSpawnableComponent))
class WATER_API UWaterTerrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Returns a list of all Primitive Components that should render as the terrain */
	virtual TArray<UPrimitiveComponent*> GetTerrainPrimitives() const;

	virtual FBox2D GetTerrainBounds() const;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	virtual bool AffectsWaterZone(AWaterZone* WaterZone) const;
protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
protected:
	/** By default, the terrain component will be rendering into any overlapping water zone.
	 * If the override is set, it will only render to that specific water zone. */
	UPROPERTY(EditAnywhere, Category = Water)
	TSoftObjectPtr<AWaterZone> WaterZoneOverride;
};

