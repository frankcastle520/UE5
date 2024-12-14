// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Lumen/LumenRadianceCache.h"
#include "Lumen/LumenViewState.h"

namespace LumenRadianceCache
{
	// Must match *.usf
	const int32 TRACE_TILE_SIZE_2D = 8;
	const int32 TRACE_TILE_ATLAS_STRITE_IN_TILES = 512;

	class FRadianceCacheSetup
	{
	public:
		TArray<FRadianceCacheClipmap> LastFrameClipmaps;
		FRDGTextureRef DepthProbeAtlasTexture;
		FRDGTextureRef FinalIrradianceAtlas;
		FRDGTextureRef ProbeOcclusionAtlas;
		FRDGTextureRef FinalRadianceAtlas;
		FRDGTextureRef RadianceProbeAtlasTextureSource;
		bool bPersistentCache;
	};

	void RenderLumenHardwareRayTracingRadianceCache(
		FRDGBuilder& GraphBuilder,
		const FScene* Scene,
		const FLumenSceneFrameTemporaries& FrameTemporaries,
		const TInlineArray<FUpdateInputs>& InputArray,
		TInlineArray<FUpdateOutputs>& OutputArray,
		const TInlineArray<FRadianceCacheSetup>& SetupOutputArray,
		const TInlineArray<FRDGBufferRef>& ProbeTraceTileAllocatorArray,
		const TInlineArray<FRDGBufferRef>& ProbeTraceTileDataArray,
		const TInlineArray<FRDGBufferRef>& ProbeTraceDataArray,
		const TInlineArray<FRDGBufferRef>& HardwareRayTracingRayAllocatorBufferArray,
		const TInlineArray<FRDGBufferRef>& TraceProbesIndirectArgsArray,
		ERDGPassFlags ComputePassFlags);

	void RenderLumenHardwareRayTracingRadianceCache_REMOVE(
		FRDGBuilder& GraphBuilder,
		const FScene* Scene,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		const FLumenCardTracingParameters& TracingParameters,
		const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
		FRadianceCacheConfiguration Configuration,
		int32 MaxNumProbes,
		int32 MaxProbeTraceTileResolution,
		FRDGBufferRef ProbeTraceData,
		FRDGBufferRef ProbeTraceTileData,
		FRDGBufferRef ProbeTraceTileAllocator,
		FRDGBufferRef TraceProbesIndirectArgs,
		FRDGBufferRef HardwareRayTracingRayAllocatorBuffer,
		FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
		FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
		FRDGTextureUAVRef DepthProbeTextureUAV,
		ERDGPassFlags ComputePassFlags);
};