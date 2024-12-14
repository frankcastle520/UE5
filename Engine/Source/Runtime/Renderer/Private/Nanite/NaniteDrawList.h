// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteMaterials.h"
#include "PrimitiveSceneInfo.h"

class FNaniteMaterialListContext
{
public:
	struct FDeferredPipelines
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo;
		TArray<FNaniteRasterPipeline, TInlineAllocator<4>> RasterPipelines;
		TArray<FNaniteShadingPipeline, TInlineAllocator<4>> ShadingPipelines;
	};

public:
	void Apply(FScene& Scene);

private:
	FNaniteMaterialSlot& GetMaterialSlotForWrite(FPrimitiveSceneInfo& PrimitiveSceneInfo, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex);
	void AddRasterBin(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteRasterBin& PrimaryRasterBin, const FNaniteRasterBin& FallbackRasterBin, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex);
	void AddShadingBin(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteShadingBin& ShadingBin, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex);

public:
	TArray<FDeferredPipelines> DeferredPipelines[ENaniteMeshPass::Num];
};
