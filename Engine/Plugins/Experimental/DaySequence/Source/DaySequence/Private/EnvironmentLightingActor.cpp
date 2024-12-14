// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnvironmentLightingActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnvironmentLightingActor)

FName AEnvironmentLightingActor::SkyAtmosphereComponentName(TEXT("SkyAtmosphere"));
FName AEnvironmentLightingActor::SkyLightComponentName(TEXT("SkyLight"));
FName AEnvironmentLightingActor::SunRootComponentName(TEXT("SunRoot"));
FName AEnvironmentLightingActor::SunComponentName(TEXT("Sun"));
FName AEnvironmentLightingActor::ExponentialHeightFogComponentName(TEXT("ExponentialHeightFog"));
FName AEnvironmentLightingActor::VolumetricCloudComponentName(TEXT("VolumetricClouds"));

AEnvironmentLightingActor::AEnvironmentLightingActor(const FObjectInitializer& Init)
: Super(Init)
{
	SkyAtmosphereComponent = CreateOptionalDefaultSubobject<USkyAtmosphereComponent>(SkyAtmosphereComponentName);
	if (SkyAtmosphereComponent)
	{
		SkyAtmosphereComponent->SetupAttachment(RootComponent);
	}

	SkyLightComponent = CreateOptionalDefaultSubobject<USkyLightComponent>(SkyLightComponentName);
	if (SkyLightComponent)
	{
		SkyLightComponent->SetupAttachment(RootComponent);
	}

	SunRootComponent = CreateOptionalDefaultSubobject<USceneComponent>(SunRootComponentName);
	if (SunRootComponent)
	{
		SunRootComponent->SetupAttachment(RootComponent);
	}

	SunComponent = CreateOptionalDefaultSubobject<UDirectionalLightComponent>(SunComponentName);
	if (SunComponent)
	{
		SunComponent->SetupAttachment(SunRootComponent ? SunRootComponent : RootComponent);
	}

	ExponentialHeightFogComponent = CreateOptionalDefaultSubobject<UExponentialHeightFogComponent>(ExponentialHeightFogComponentName);
	if (ExponentialHeightFogComponent)
	{
		ExponentialHeightFogComponent->SetupAttachment(RootComponent);
	}

	VolumetricCloudComponent = CreateOptionalDefaultSubobject<UVolumetricCloudComponent>(VolumetricCloudComponentName);
	if (VolumetricCloudComponent)
	{
		VolumetricCloudComponent->SetupAttachment(RootComponent);
	}
}

