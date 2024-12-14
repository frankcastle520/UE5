// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MeshCardRepresentation.h"
#include "SceneView.h"
#include "MeshPassProcessor.h"

class FScene;
class FLumenPrimitiveGroup;
class FLumenCard;
struct FNaniteShadingBin;
struct FNaniteShadingCommand;

struct FCardCaptureAtlas
{
	FIntPoint Size;
	FRDGTextureRef Albedo;
	FRDGTextureRef Normal;
	FRDGTextureRef Emissive;
	FRDGTextureRef DepthStencil;
};

struct FResampledCardCaptureAtlas
{
	FIntPoint Size;
	FRDGTextureRef DirectLighting = nullptr;
	FRDGTextureRef IndirectLighting = nullptr;
	FRDGTextureRef NumFramesAccumulated = nullptr;
	FRDGBufferRef TileShadowDownsampleFactor = nullptr;
};

class FCardPageRenderData
{
public:
	int32 PrimitiveGroupIndex = INDEX_NONE;

	// CardData
	const int32 CardIndex = INDEX_NONE;
	const int32 PageTableIndex = INDEX_NONE;
	FVector4f CardUVRect;
	FIntRect CardCaptureAtlasRect;
	FIntRect SurfaceCacheAtlasRect;

	FLumenCardOBBd CardWorldOBB;

	FViewMatrices ViewMatrices;
	FMatrix ProjectionMatrixUnadjustedForRHI;

	int32 StartMeshDrawCommandIndex = 0;
	int32 NumMeshDrawCommands = 0;

	TArray<uint32, SceneRenderingAllocator> NaniteInstanceIds;
	TArray<FNaniteShadingBin, SceneRenderingAllocator> NaniteShadingBins;
	float NaniteLODScaleFactor = 1.0f;

	bool bResampleLastLighting = false;

	// Non-Nanite mesh inclusive instance ranges to draw
	TArray<uint32, SceneRenderingAllocator> InstanceRuns;

	FCardPageRenderData(
		const FViewInfo& InMainView,
		const FLumenCard& InLumenCard,
		FVector4f InCardUVRect,
		FIntRect InCardCaptureAtlasRect,
		FIntRect InSurfaceCacheAtlasRect,
		int32 InPrimitiveGroupIndex,
		int32 InCardIndex,
		int32 InCardPageIndex,
		bool bResampleLastLighting);

	~FCardPageRenderData();

	void UpdateViewMatrices(const FViewInfo& MainView);
	void PatchView(const FScene* Scene, FViewInfo* View) const;

	inline bool HasNanite() const
	{
		return NaniteShadingBins.Num() > 0 && NaniteInstanceIds.Num() > 0;
	}
};

namespace LumenScene
{

bool HasPrimitiveNaniteMeshBatches(const FPrimitiveSceneProxy* Proxy);

void AllocateCardCaptureAtlas(
	FRDGBuilder& GraphBuilder,
	FIntPoint CardCaptureAtlasSize,
	FCardCaptureAtlas& CardCaptureAtlas,
	EShaderPlatform ShaderPlatform
);

void AddCardCaptureDraws(
	const FScene* Scene,
	FCardPageRenderData& CardPageRenderData,
	const FLumenPrimitiveGroup& PrimitiveGroup,
	TConstArrayView<const FPrimitiveSceneInfo*> SceneInfoPrimitives,
	FMeshCommandOneFrameArray& VisibleMeshCommands,
	TArray<int32, SceneRenderingAllocator>& PrimitiveIds
);

}

BEGIN_UNIFORM_BUFFER_STRUCT(FLumenCardOutputs, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTarget0)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTarget1)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutTarget2)
END_UNIFORM_BUFFER_STRUCT()

namespace Nanite
{
	void RecordLumenCardParameters(
		FRHIBatchedShaderParameters& ShaderParameters,
		FNaniteShadingCommand& ShadingCommand,
		TUniformBufferRef<FLumenCardOutputs> Outputs
	);
}
