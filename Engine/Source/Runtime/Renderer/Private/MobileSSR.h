// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphDefinitions.h"
#include "SceneRendering.h"
#include "ScreenSpaceRayTracing.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMobileScreenSpaceReflectionParams, )
SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneColor)
SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)
SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZB)
SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
SHADER_PARAMETER(FVector4f, HZBUvFactorAndInvFactor)
SHADER_PARAMETER(FVector4f, PrevScreenPositionScaleBias)
SHADER_PARAMETER(FVector4f, PrevSceneColorBilinearUVMinMax)
SHADER_PARAMETER(FVector4f, IntensityAndExposureCorrection) // .x = Intensity, .y = PrevSceneColorPreExposureInv, .z = View.FinalPostProcessSettings.ScreenSpaceReflectionMaxRoughness, .w = 2DivMaxRoughness
SHADER_PARAMETER(uint32, NoiseIndex)
END_SHADER_PARAMETER_STRUCT()

enum class EMobileSSRQuality
{
	Disabled,
	Low,
	Medium,
	MAX
};

bool IsMobileSSREnabled(const FViewInfo& View);
EMobileSSRQuality ActiveMobileSSRQuality(const FViewInfo& View, bool bHasVelocityTexture);
void SetupMobileSSRParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FMobileScreenSpaceReflectionParams& Params);
