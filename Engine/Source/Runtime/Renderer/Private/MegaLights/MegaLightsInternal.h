// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererPrivate.h"
#include "BlueNoise.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMegaLightsParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(LightFunctionAtlas::FLightFunctionAtlasGlobalParameters, LightFunctionAtlas)
	SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER(FIntPoint, SampleViewMin)
	SHADER_PARAMETER(FIntPoint, SampleViewSize)
	SHADER_PARAMETER(FIntPoint, DownsampledViewMin)
	SHADER_PARAMETER(FIntPoint, DownsampledViewSize)
	SHADER_PARAMETER(FIntPoint, NumSamplesPerPixel)
	SHADER_PARAMETER(FIntPoint, NumSamplesPerPixelDivideShift)
	SHADER_PARAMETER(FVector2f, DownsampledBufferInvSize)
	SHADER_PARAMETER(uint32, DownsampleFactor)
	SHADER_PARAMETER(uint32, MegaLightsStateFrameIndex)
	SHADER_PARAMETER(float, MinSampleWeight)
	SHADER_PARAMETER(int32, TileDataStride)
	SHADER_PARAMETER(int32, DownsampledTileDataStride)
	SHADER_PARAMETER(float, TemporalMaxFramesAccumulated)
	SHADER_PARAMETER(float, TemporalNeighborhoodClampScale)
	SHADER_PARAMETER(uint32, bOverrideCursorPosition)
	SHADER_PARAMETER(int32, DebugMode)
	SHADER_PARAMETER(int32, DebugLightId)
	SHADER_PARAMETER(int32, DebugVisualizeLight)
	SHADER_PARAMETER(int32, UseIESProfiles)
	SHADER_PARAMETER(int32, UseLightFunctionAtlas)
	SHADER_PARAMETER(float, VolumeMinSampleWeight)
	SHADER_PARAMETER(int32, VolumeDebugMode)
	SHADER_PARAMETER(int32, VolumeDebugSliceIndex)
	SHADER_PARAMETER(FIntVector, NumSamplesPerVoxel)
	SHADER_PARAMETER(FIntVector, NumSamplesPerVoxelDivideShift)
	SHADER_PARAMETER(FMatrix44f, UnjitteredClipToTranslatedWorld)
	SHADER_PARAMETER(FIntVector, DownsampledVolumeViewSize)
	SHADER_PARAMETER(FIntVector, VolumeViewSize)
	SHADER_PARAMETER(FIntVector, VolumeSampleViewSize)
	SHADER_PARAMETER(FVector3f, MegaLightsVolumeZParams)
	SHADER_PARAMETER(uint32, MegaLightsVolumePixelSize)
	SHADER_PARAMETER(uint32, MegaLightsVolumePixelSizeShift)
	SHADER_PARAMETER(FVector3f, VolumeFrameJitterOffset)
	SHADER_PARAMETER(float, VolumePhaseG)
	SHADER_PARAMETER(float, VolumeInverseSquaredLightDistanceBiasScale)
	SHADER_PARAMETER(float, LightSoftFading)
	SHADER_PARAMETER(uint32, UseHZBOcclusionTest)

	SHADER_PARAMETER(FVector2f, HZBSize)
	SHADER_PARAMETER(FVector2f, HZBViewSize)
	SHADER_PARAMETER(FIntRect, HZBViewRect)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)

	SHADER_PARAMETER(FIntPoint, VisibleLightHashViewMinInTiles)
	SHADER_PARAMETER(FIntPoint, VisibleLightHashViewSizeInTiles)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DownsampledTileMask)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DownsampledSceneDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UNORM float3>, DownsampledSceneWorldNormal)
END_SHADER_PARAMETER_STRUCT()

// Internal functions, don't use outside of the MegaLights
namespace MegaLights
{
	void RayTraceLightSamples(
		const FSceneViewFamily& ViewFamily,
		const FViewInfo& View, int32 ViewIndex,
		FRDGBuilder& GraphBuilder,
		const FSceneTextures& SceneTextures,
		const FVirtualShadowMapArray* VirtualShadowMapArray,
		const FIntPoint SampleBufferSize,
		FRDGTextureRef LightSamples,
		FRDGTextureRef LightSampleUV,
		FRDGTextureRef LightSampleRayDistance,
		FIntVector VolumeSampleBufferSize,
		FRDGTextureRef VolumeLightSamples,
		const FMegaLightsParameters& MegaLightsParameters
	);

	bool UseWaveOps(EShaderPlatform ShaderPlatform);
	int32 GetDebugMode();
	int32 GetVolumeDebugMode();

	void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment);
};