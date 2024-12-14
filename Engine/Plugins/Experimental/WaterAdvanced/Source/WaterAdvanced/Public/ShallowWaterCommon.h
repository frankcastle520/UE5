// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShallowWaterCommon.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogShallowWater, Log, All);

class UPhysicsAsset;

USTRUCT(BlueprintType)
struct WATERADVANCED_API FShallowWaterSimParameters 
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Shallow Water")
	float WorldGridSize = 2000.f;
	UPROPERTY(EditAnywhere, Category = "Shallow Water")
	int32 ResolutionMaxAxis = 512;	
};

USTRUCT(BlueprintType)
struct WATERADVANCED_API FShallowWaterPhysicsAssetOverride
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, Category = "Shallow Water")
	TSoftObjectPtr<UPhysicsAsset> PhysicsAsset;
};