// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DaySequenceProvider.h"
#include "EnvironmentLightingActor.generated.h"

class USkyAtmosphereComponent;
class USkyLightComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UVolumetricCloudComponent;

UCLASS()
class AEnvironmentLightingActor : public ADaySequenceProvider
{
	GENERATED_BODY()
public:
	AEnvironmentLightingActor(const FObjectInitializer& Init);

protected:
	// Components
	UPROPERTY(Category=Environment, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true", NoEditInline))
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphereComponent;

	UPROPERTY(Category=Environment, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true", NoEditInline))
	TObjectPtr<USkyLightComponent> SkyLightComponent;

	UPROPERTY(Category=Environment, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true", NoEditInline))
	TObjectPtr<USceneComponent> SunRootComponent;

	UPROPERTY(Category=Environment, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true", NoEditInline))
	TObjectPtr<UDirectionalLightComponent> SunComponent;

	UPROPERTY(Category=Environment, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true", NoEditInline))
	TObjectPtr<UExponentialHeightFogComponent> ExponentialHeightFogComponent;

	UPROPERTY(Category=Environment, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true", NoEditInline))
	TObjectPtr<UVolumetricCloudComponent> VolumetricCloudComponent;

	static FName SkyAtmosphereComponentName;
	static FName SkyLightComponentName;
	static FName SunRootComponentName;
	static FName SunComponentName;
	static FName ExponentialHeightFogComponentName;
	static FName VolumetricCloudComponentName;
};
