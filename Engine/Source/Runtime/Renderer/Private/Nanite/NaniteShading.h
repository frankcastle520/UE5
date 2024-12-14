// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteSceneProxy.h"

#include "SceneRendering.h"
#include "Stats/Stats.h"

struct FNaniteShadingCommands;
class  FLumenCardPassUniformParameters;
class  FCardPageRenderData;
class  FSceneRenderer;

namespace Nanite
{

struct FRasterResults;
struct FRasterContext;

struct FShadeBinning
{
	FRDGBufferRef ShadingBinData  = nullptr;
	FRDGBufferRef ShadingBinArgs  = nullptr;
	FRDGBufferRef ShadingBinStats = nullptr;

	FRDGTextureRef FastClearVisualize = nullptr;

	uint32 DataByteOffset = 0u;
};

FShadeBinning ShadeBinning(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntRect InViewRect,
	const FNaniteShadingCommands& ShadingCommands,
	const FRasterResults& RasterResults,
	const TConstArrayView<FRDGTextureRef> ClearTargets
);

enum class EBuildShadingCommandsMode : uint8
{
	Default = 0,
	Custom,
};

void BuildShadingCommands(
	FRDGBuilder& GraphBuilder,
	FScene& Scene,
	ENaniteMeshPass::Type MeshPass,
	FNaniteShadingCommands& ShadingCommands,
	EBuildShadingCommandsMode Mode = EBuildShadingCommandsMode::Default
);

bool LoadBasePassPipeline(
	const FScene& Scene,
	FSceneProxyBase* SceneProxy,
	FSceneProxyBase::FMaterialSection& Section,
	FNaniteShadingPipeline& ShadingPipeline
);

bool LoadLumenCardPipeline(
	const FScene& Scene,
	FSceneProxyBase* SceneProxy,
	FSceneProxyBase::FMaterialSection& Section,
	FNaniteShadingPipeline& ShadingPipeline
);

void DispatchBasePass(
	FRDGBuilder& GraphBuilder,
	FNaniteShadingCommands& ShadingCommands,
	const FSceneRenderer& SceneRenderer,
	const FSceneTextures& SceneTextures,
	const FRenderTargetBindingSlots& BasePassRenderTargets,
	const FDBufferTextures& DBufferTextures,
	const FScene& Scene,
	const FViewInfo& View,
	const uint32 ViewIndex,
	const FRasterResults& RasterResults
);

void CollectBasePassShadingPSOInitializers(
	const FSceneTexturesConfig& SceneTexturesConfig,
	const FPSOPrecacheVertexFactoryData& VertexFactoryData,
	const FMaterial& Material,
	const FPSOPrecacheParams& PreCacheParams,
	ERHIFeatureLevel::Type FeatureLevel,
	EShaderPlatform ShaderPlatform,
	int32 PSOCollectorIndex,
	TArray<FPSOPrecacheData>& PSOInitializers
);

void CollectLumenCardPSOInitializers(
	const FSceneTexturesConfig& SceneTexturesConfig,
	const FPSOPrecacheVertexFactoryData& VertexFactoryData,
	const FMaterial& Material,
	const FPSOPrecacheParams& PreCacheParams,
	ERHIFeatureLevel::Type FeatureLevel,
	EShaderPlatform ShaderPlatform,
	int32 PSOCollectorIndex,
	TArray<FPSOPrecacheData>& PSOInitializers
);

extern bool HasNoDerivativeOps(FRHIComputeShader* ComputeShaderRHI);
extern uint32 PackMaterialBitFlags(const FMaterial& Material, uint32 BoundTargetMask, bool bNoDerivativeOps);

} // Nanite

void DispatchLumenMeshCapturePass(
	FRDGBuilder& GraphBuilder,
	FScene& Scene,
	FViewInfo* SharedView,
	TArrayView<const FCardPageRenderData> CardPagesToRender,
	const Nanite::FRasterResults& RasterResults,
	const Nanite::FRasterContext& RasterContext,
	FLumenCardPassUniformParameters* PassUniformParameters,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects,
	FIntPoint ViewportSize,
	FRDGTextureRef AlbedoAtlasTexture,
	FRDGTextureRef NormalAtlasTexture,
	FRDGTextureRef EmissiveAtlasTexture,
	FRDGTextureRef DepthAtlasTexture
);