// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "MetaHumanSDKSettings.generated.h"

/**
 *
 */
UCLASS(defaultconfig, config = MetaHumanSDK, meta = (DisplayName = "MetaHuman SDK"))
class METAHUMANSDKEDITOR_API UMetaHumanSDKSettings : public UObject
{
	GENERATED_BODY()

public:
	// The URL for fetching version information and release notes
	UPROPERTY(Config)
	FString VersionServiceBaseUrl;

	// The asset path for importing Cinematic MetaHumans.
	UPROPERTY(EditAnywhere, Config, Category = "MetaHuman Import Paths", meta = (ContentDir, DisplayName = "Cinematic Characters"))
	FDirectoryPath CinematicImportPath{TEXT("/Game/MetaHumans")};

	// The asset path for importing Optimized MetaHumans.
	UPROPERTY(EditAnywhere, Config, Category = "MetaHuman Import Paths", meta = (ContentDir, DisplayName = "Optimized Characters"))
	FDirectoryPath OptimizedImportPath{TEXT("/Game/MetaHumans")};
};
