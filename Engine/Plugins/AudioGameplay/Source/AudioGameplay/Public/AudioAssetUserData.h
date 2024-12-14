// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"
#include "GameplayTagContainer.h"
#include "AudioAssetUserData.generated.h"

/**
 * UAudioAssetUserData - Base class for user data being attached to audio assets
 */
UCLASS(Blueprintable)
class AUDIOGAMEPLAY_API UAudioAssetUserData : public UAssetUserData
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tags", meta = (Categories = "Audio.Metadata"))
	FGameplayTagContainer MetadataTags;
};
