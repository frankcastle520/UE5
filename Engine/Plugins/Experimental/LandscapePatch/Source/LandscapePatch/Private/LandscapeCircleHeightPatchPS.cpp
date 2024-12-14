// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeCircleHeightPatchPS.h"

#include "PixelShaderUtils.h"
#include "DataDrivenShaderPlatformInfo.h"

bool FLandscapeCircleHeightPatchPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	// Apparently landscape requires a particular feature level
	return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
		&& !IsConsolePlatform(Parameters.Platform)
		&& !IsMetalMobilePlatform(Parameters.Platform);
}

void FLandscapeCircleHeightPatchPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("CIRCLE_HEIGHT_PATCH"), 1);
}

void FLandscapeCircleHeightPatchPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* Parameters, const FIntRect& DestinationBounds)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FLandscapeCircleHeightPatchPS> PixelShader(ShaderMap);

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("LandscapeCircleHeightPatch"),
		PixelShader,
		Parameters,
		DestinationBounds);
}

void FLandscapeCircleVisibilityPatchPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("CIRCLE_VISIBILITY_PATCH"), 1);
}

void FLandscapeCircleVisibilityPatchPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* Parameters, const FIntRect& DestinationBounds)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FLandscapeCircleVisibilityPatchPS> PixelShader(ShaderMap);

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("LandscapeCircleVisibilityPatch"),
		PixelShader,
		Parameters,
		DestinationBounds);
}

IMPLEMENT_GLOBAL_SHADER(FLandscapeCircleHeightPatchPS, "/Plugin/LandscapePatch/Private/LandscapeCircleHeightPatchPS.usf", "ApplyLandscapeCircleHeightPatch", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FLandscapeCircleVisibilityPatchPS, "/Plugin/LandscapePatch/Private/LandscapeCircleHeightPatchPS.usf", "ApplyLandscapeCircleVisibilityPatch", SF_Pixel);
