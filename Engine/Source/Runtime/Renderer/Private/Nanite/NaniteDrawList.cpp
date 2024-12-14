// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDrawList.h"
#include "BasePassRendering.h"
#include "NaniteSceneProxy.h"
#include "NaniteShading.h"
#include "NaniteVertexFactory.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"

int32 GNaniteAllowProgrammableDistances = 1;
static FAutoConsoleVariableRef CVarNaniteAllowProgrammableDistances(
	TEXT("r.Nanite.AllowProgrammableDistances"),
	GNaniteAllowProgrammableDistances,
	TEXT("Whether or not to allow disabling of Nanite programmable raster features (World Position Offset, Pixel Depth Offset, ")
	TEXT("Masked Opaque, or Displacement) at a distance from the camera."),
	ECVF_ReadOnly
);

static const TCHAR* NanitePSOCollectorName = TEXT("NaniteMesh");

class FNanitePSOCollector : public IPSOCollector
{
public:
	FNanitePSOCollector(ERHIFeatureLevel::Type InFeatureLevel) : 
		IPSOCollector(FPSOCollectorCreateManager::GetIndex(GetFeatureLevelShadingPath(InFeatureLevel), NanitePSOCollectorName)),
		FeatureLevel(InFeatureLevel)
	{
	}

	virtual void CollectPSOInitializers(
		const FSceneTexturesConfig& SceneTexturesConfig,
		const FMaterial& Material,
		const FPSOPrecacheVertexFactoryData& VertexFactoryData,
		const FPSOPrecacheParams& PreCacheParams,
		TArray<FPSOPrecacheData>& PSOInitializers
	) override final;

private:

	ERHIFeatureLevel::Type FeatureLevel;
};

void FNanitePSOCollector::CollectPSOInitializers(
	const FSceneTexturesConfig& SceneTexturesConfig,
	const FMaterial& Material,
	const FPSOPrecacheVertexFactoryData& VertexFactoryData,
	const FPSOPrecacheParams& PreCacheParams,
	TArray<FPSOPrecacheData>& PSOInitializers
)
{
	// Make sure Nanite rendering is supported.
	EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);
	if (!UseNanite(ShaderPlatform))
	{
		return;
	}

	// Only support the Nanite vertex factory type.
	if (VertexFactoryData.VertexFactoryType != &FNaniteVertexFactory::StaticType)
	{
		return;
	}

	// Check if Nanite can be used by this material
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	bool bShouldDraw = Nanite::IsSupportedBlendMode(Material) && Nanite::IsSupportedMaterialDomain(Material.GetMaterialDomain());
	if (!bShouldDraw)
	{
		return;
	}

	// Nanite passes always use the forced fixed vertex element and not custom default vertex declaration even if it's provided
	FPSOPrecacheVertexFactoryData NaniteVertexFactoryData = VertexFactoryData;
	NaniteVertexFactoryData.CustomDefaultVertexDeclaration = nullptr;

	Nanite::CollectBasePassShadingPSOInitializers(SceneTexturesConfig, NaniteVertexFactoryData, Material, PreCacheParams, FeatureLevel, ShaderPlatform, PSOCollectorIndex, PSOInitializers);
	Nanite::CollectRasterPSOInitializers(SceneTexturesConfig, Material, PreCacheParams, ShaderPlatform, PSOCollectorIndex, PSOInitializers);
	Nanite::CollectLumenCardPSOInitializers(SceneTexturesConfig, NaniteVertexFactoryData, Material, PreCacheParams, FeatureLevel, ShaderPlatform, PSOCollectorIndex, PSOInitializers);
}

IPSOCollector* CreateNanitePSOCollector(ERHIFeatureLevel::Type FeatureLevel)
{
	if (DoesPlatformSupportNanite(GetFeatureLevelShaderPlatform(FeatureLevel)))
	{
		return new FNanitePSOCollector(FeatureLevel);
	}
	else
	{
		return nullptr;
	}
}
FRegisterPSOCollectorCreateFunction RegisterNanitePSOCollector(&CreateNanitePSOCollector, EShadingPath::Deferred, NanitePSOCollectorName);

FNaniteMaterialSlot& FNaniteMaterialListContext::GetMaterialSlotForWrite(FPrimitiveSceneInfo& PrimitiveSceneInfo, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex)
{	
	TArray<FNaniteMaterialSlot>& MaterialSlots = PrimitiveSceneInfo.NaniteMaterialSlots[MeshPass];

	// Initialize material slots if they haven't been already
	// NOTE: Lazily initializing them like this prevents adding material slots for primitives that have no bins in the pass
	if (MaterialSlots.Num() == 0)
	{
		check(PrimitiveSceneInfo.Proxy->IsNaniteMesh());
		check(PrimitiveSceneInfo.NaniteRasterBins[MeshPass].Num() == 0);
		check(PrimitiveSceneInfo.NaniteShadingBins[MeshPass].Num() == 0);

		auto* NaniteSceneProxy = static_cast<const Nanite::FSceneProxyBase*>(PrimitiveSceneInfo.Proxy);
		const int32 NumMaterialSections = NaniteSceneProxy->GetMaterialSections().Num();

		MaterialSlots.SetNumUninitialized(NumMaterialSections);
		FMemory::Memset(MaterialSlots.GetData(), 0xFF, NumMaterialSections * MaterialSlots.GetTypeSize());
	}

	check(MaterialSlots.IsValidIndex(SectionIndex));
	return MaterialSlots[SectionIndex];
}

void FNaniteMaterialListContext::AddShadingBin(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteShadingBin& ShadingBin, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex)
{
	FNaniteMaterialSlot& MaterialSlot = GetMaterialSlotForWrite(PrimitiveSceneInfo, MeshPass, SectionIndex);
	check(MaterialSlot.ShadingBin == 0xFFFFu);
	MaterialSlot.ShadingBin = ShadingBin.BinIndex;

	PrimitiveSceneInfo.NaniteShadingBins[MeshPass].Add(ShadingBin);
}

void FNaniteMaterialListContext::AddRasterBin(
	FPrimitiveSceneInfo& PrimitiveSceneInfo,
	const FNaniteRasterBin& PrimaryRasterBin,
	const FNaniteRasterBin& FallbackRasterBin,
	ENaniteMeshPass::Type MeshPass,
	uint8 SectionIndex)
{
	check(PrimaryRasterBin.IsValid());
	
	FNaniteMaterialSlot& MaterialSlot = GetMaterialSlotForWrite(PrimitiveSceneInfo, MeshPass, SectionIndex);
	check(MaterialSlot.RasterBin == 0xFFFFu);
	MaterialSlot.RasterBin = PrimaryRasterBin.BinIndex;
	MaterialSlot.FallbackRasterBin = FallbackRasterBin.BinIndex;
	
	PrimitiveSceneInfo.NaniteRasterBins[MeshPass].Add(PrimaryRasterBin);
	if (FallbackRasterBin.IsValid())
	{
		PrimitiveSceneInfo.NaniteRasterBins[MeshPass].Add(FallbackRasterBin);
	}
}

void FNaniteMaterialListContext::Apply(FScene& Scene)
{
	check(IsInParallelRenderingThread());

	for (int32 MeshPassIndex = 0; MeshPassIndex < ENaniteMeshPass::Num; ++MeshPassIndex)
	{
		FNaniteRasterPipelines& RasterPipelines = Scene.NaniteRasterPipelines[MeshPassIndex];
		FNaniteShadingPipelines& ShadingPipelines = Scene.NaniteShadingPipelines[MeshPassIndex];
		FNaniteVisibility& Visibility = Scene.NaniteVisibility[MeshPassIndex];

		for (const FDeferredPipelines& PipelinesCommand : DeferredPipelines[MeshPassIndex])
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = PipelinesCommand.PrimitiveSceneInfo;
			FNaniteVisibility::PrimitiveRasterBinType* RasterBins = Visibility.GetRasterBinReferences(PrimitiveSceneInfo);
			FNaniteVisibility::PrimitiveShadingBinType* ShadingBins = Visibility.GetShadingBinReferences(PrimitiveSceneInfo);

			check((PipelinesCommand.RasterPipelines.Num() == PipelinesCommand.ShadingPipelines.Num()));
			const int32 MaterialSectionCount = PipelinesCommand.RasterPipelines.Num();
			for (int32 MaterialSectionIndex = 0; MaterialSectionIndex < MaterialSectionCount; ++MaterialSectionIndex)
			{
				// Register raster bin
				{
					const FNaniteRasterPipeline& RasterPipeline = PipelinesCommand.RasterPipelines[MaterialSectionIndex];
					FNaniteRasterBin PrimaryRasterBin = RasterPipelines.Register(RasterPipeline);

					// Check to register a fallback bin (used to disable programmable functionality at a distance)
					FNaniteRasterBin FallbackRasterBin;
					FNaniteRasterPipeline FallbackRasterPipeline;
					if (GNaniteAllowProgrammableDistances && RasterPipeline.GetFallbackPipeline(FallbackRasterPipeline))
					{
						FallbackRasterBin = RasterPipelines.Register(FallbackRasterPipeline);
					}

					AddRasterBin(*PrimitiveSceneInfo, PrimaryRasterBin, FallbackRasterBin, ENaniteMeshPass::Type(MeshPassIndex), uint8(MaterialSectionIndex));

					if (RasterBins)
					{
						RasterBins->Add(FNaniteVisibility::FRasterBin{ PrimaryRasterBin.BinIndex, FallbackRasterBin.BinIndex });
					}
				}

				// Register shading bin
				{
					const FNaniteShadingPipeline& ShadingPipeline = PipelinesCommand.ShadingPipelines[MaterialSectionIndex];
					const FNaniteShadingBin ShadingBin = ShadingPipelines.Register(ShadingPipeline);
					AddShadingBin(*PrimitiveSceneInfo, ShadingBin, ENaniteMeshPass::Type(MeshPassIndex), uint8(MaterialSectionIndex));

					if (ShadingBins)
					{
						ShadingBins->Add(FNaniteVisibility::FShadingBin{ ShadingBin.BinIndex });
					}
				}
			}

			// This will register the primitive's raster bins for custom depth, if necessary
			if (MeshPassIndex == ENaniteMeshPass::BasePass)
			{
				PrimitiveSceneInfo->RefreshNaniteRasterBins();
			}
		}
	}

}
