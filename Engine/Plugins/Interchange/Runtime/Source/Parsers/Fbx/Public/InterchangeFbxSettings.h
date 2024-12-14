// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InterchangeAnimationDefinitions.h"
#include "InterchangeAnimationTrackSetNode.h"


#include "Engine/DeveloperSettings.h"

#include "InterchangeFbxSettings.generated.h"

UCLASS(config = Interchange, meta = (DisplayName = "FBX Settings"))
class INTERCHANGEFBXPARSER_API UInterchangeFbxSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UInterchangeFbxSettings();

	/** Search for a predefined property track, if the property has been found it returns it, otherwise we search for a custom property track*/
	EInterchangePropertyTracks GetPropertyTrack(const FString& PropertyName) const;

	UPROPERTY(EditAnywhere, config, Category = "FBX | Property Tracks")
	TMap<FString, EInterchangePropertyTracks > CustomPropertyTracks;

private:
	TMap<FString, EInterchangePropertyTracks > PredefinedPropertyTracks;
};