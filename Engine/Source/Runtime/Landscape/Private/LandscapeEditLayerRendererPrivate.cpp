// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditLayerRendererPrivate.h"

#include "Algo/AllOf.h"
#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "LandscapeEditResourcesSubsystem.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeInfo.h"
#include "RHIAccess.h"


#if WITH_EDITOR

// ----------------------------------------------------------------------------------

void ULandscapeDefaultEditLayerRenderer::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
	UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutSupportedTargetTypeState, UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutEnabledTargetTypeState, TArray<TSet<FName>>& OutRenderGroups) const
{
	// Supports all heightmaps and weightmaps:
	OutSupportedTargetTypeState.SetTargetTypeMask(ELandscapeToolTargetTypeFlags::All);
	TSet<FName> WeightmapLayerNames = GatherWeightmapLayerNames(InLandscapeInfo);
	Algo::ForEach(WeightmapLayerNames, [&OutSupportedTargetTypeState](FName InLayerName) { OutSupportedTargetTypeState.AddWeightmap(InLayerName); });
	OutEnabledTargetTypeState = OutSupportedTargetTypeState;
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ULandscapeDefaultEditLayerRenderer::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	FEditLayerTargetTypeState OutputTargetTypeState(ELandscapeToolTargetTypeFlags::All);
	// Standard renderer : we don't need more than the component itself to render properly:
	FInputWorldArea InputWorldArea(FInputWorldArea::CreateLocalComponent());
	// The renderer only writes into the component itself (i.e. it renders to the area that it's currently being asked to render to):
	FOutputWorldArea OutputWorldArea(FOutputWorldArea::CreateLocalComponent());
	// The renderer is only providing default data for existing weightmaps so it doesn't generate new ones, hence we pass bModifyExistingWeightmapsOnly to true : 
	return { FEditLayerRenderItem(OutputTargetTypeState, InputWorldArea, OutputWorldArea, /*bModifyExistingWeightmapsOnly = */true) };
}

FString ULandscapeDefaultEditLayerRenderer::GetEditLayerRendererDebugName() const 
{
	return TEXT("Default");
}

void ULandscapeDefaultEditLayerRenderer::RenderLayer(FRenderParams& InRenderParams) 
{
	using namespace UE::Landscape::EditLayers;

	FMergeRenderContext* RenderContext = InRenderParams.MergeRenderContext;
	const FMergeRenderBatch* RenderBatch = RenderContext->GetCurrentRenderBatch();

	RenderContext->CycleBlendRenderTargets(/*InDesiredWriteAccess = */ERHIAccess::RTV);
	ULandscapeScratchRenderTarget* WriteRT = RenderContext->GetBlendRenderTargetWrite();

	// Start from a blank canvas so that the first layer is blended with nothing underneath :
	WriteRT->Clear();

	// Render the components of the batch for each target layer into the "pseudo-stencil" buffer, so that it can be sampled by users as a UTextures in UMaterials and such : 
	RenderContext->RenderValidityRenderTargets(*RenderBatch);
}

TSet<FName> ULandscapeDefaultEditLayerRenderer::GatherWeightmapLayerNames(const ULandscapeInfo* InLandscapeInfo) const
{
	TSet<FName> WeightmapLayerNames;
	// This renderer supports all weightmaps:
	Algo::TransformIf(InLandscapeInfo->Layers, WeightmapLayerNames,
		[](const FLandscapeInfoLayerSettings& InInfoLayerSettings) { return (InInfoLayerSettings.LayerInfoObj != nullptr); },
		[](const FLandscapeInfoLayerSettings& InInfoLayerSettings) { return InInfoLayerSettings.LayerInfoObj->LayerName; });
	return WeightmapLayerNames;
}


// ----------------------------------------------------------------------------------

void ULandscapeHeightmapNormalsEditLayerRenderer::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
	UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutSupportedTargetTypeState, UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutEnabledTargetTypeState, TArray<TSet<FName>>& OutRenderGroups) const
{
	// Only relevant for heightmaps :
	OutSupportedTargetTypeState.SetTargetTypeMask(ELandscapeToolTargetTypeFlags::Heightmap);
	OutEnabledTargetTypeState.SetTargetTypeMask(ELandscapeToolTargetTypeFlags::Heightmap);
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ULandscapeHeightmapNormalsEditLayerRenderer::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	// Only relevant for heightmaps :
	FEditLayerTargetTypeState OutputTargetTypeState(ELandscapeToolTargetTypeFlags::Heightmap);
	// The input is relative and its size is equal to the size of 3x3 landscape components so that we gather all neighbor landscape components around each component : 
	FInputWorldArea InputWorldArea(FInputWorldArea::CreateLocalComponent(FIntRect(-1, -1, 1, 1)));
	// The renderer only writes into the component itself (i.e. it renders to the area that it's currently being asked to render to):
	FOutputWorldArea OutputWorldArea(FOutputWorldArea::CreateLocalComponent());
	return { FEditLayerRenderItem(OutputTargetTypeState, InputWorldArea, OutputWorldArea, /*bModifyExistingWeightmapsOnly = */false) };
}

FString ULandscapeHeightmapNormalsEditLayerRenderer::GetEditLayerRendererDebugName() const
{
	return TEXT("Normals");
}


// ----------------------------------------------------------------------------------

void ULandscapeWeightmapWeightBlendedLayersRenderer::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
	UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutSupportedTargetTypeState, UE::Landscape::EditLayers::FEditLayerTargetTypeState& OutEnabledTargetTypeState, TArray<TSet<FName>>& OutRenderGroups) const
{
	// Only relevant for weightmaps :
	OutSupportedTargetTypeState.SetTargetTypeMask(ELandscapeToolTargetTypeFlags::Weightmap);
	TSet<FName> WeightBlendedWeightmapLayerNames = GatherWeightBlendedWeightmapLayerNames(InLandscapeInfo);
	Algo::ForEach(WeightBlendedWeightmapLayerNames, [&OutSupportedTargetTypeState](FName InLayerName) { OutSupportedTargetTypeState.AddWeightmap(InLayerName); });
	OutEnabledTargetTypeState = OutSupportedTargetTypeState;

	check(Algo::AllOf(WeightBlendedWeightmapLayerNames, [InLandscapeInfo](FName InTargetLayerName)
	{
		return InLandscapeInfo->Layers.ContainsByPredicate([InTargetLayerName](const FLandscapeInfoLayerSettings& InInfoLayerSettings) { return (InInfoLayerSettings.GetLayerName() == InTargetLayerName); });
	}));

	// Now fill in the render groups :
	if (!WeightBlendedWeightmapLayerNames.IsEmpty())
	{
		OutRenderGroups.Add(WeightBlendedWeightmapLayerNames);
	}
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ULandscapeWeightmapWeightBlendedLayersRenderer::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	// Only relevant for weightmaps :
	FEditLayerTargetTypeState OutputTargetTypeState(ELandscapeToolTargetTypeFlags::Weightmap, GatherWeightBlendedWeightmapLayerNames(InLandscapeInfo).Array());
	// Standard renderer : we don't need more than the component itself to render properly:
	FInputWorldArea InputWorldArea(FInputWorldArea::CreateLocalComponent());
	// The renderer only writes into the component itself (i.e. it renders to the area that it's currently being asked to render to):
	FOutputWorldArea OutputWorldArea(FOutputWorldArea::CreateLocalComponent());
	// The renderer is only blending existing weightmaps so it doesn't generate new ones, hence we pass bModifyExistingWeightmapsOnly to true : 
	return { FEditLayerRenderItem(OutputTargetTypeState, InputWorldArea, OutputWorldArea, /*bModifyExistingWeightmapsOnly = */true) };
}

FString ULandscapeWeightmapWeightBlendedLayersRenderer::GetEditLayerRendererDebugName() const 
{
	return TEXT("Final Weight Blend");
}

TSet<FName> ULandscapeWeightmapWeightBlendedLayersRenderer::GatherWeightBlendedWeightmapLayerNames(const ULandscapeInfo* InLandscapeInfo) const
{
	TSet<FName> WeightBlendedWeightmapLayerNames;
	Algo::TransformIf(InLandscapeInfo->Layers, WeightBlendedWeightmapLayerNames,
		[](const FLandscapeInfoLayerSettings& InInfoLayerSettings) { return ((InInfoLayerSettings.LayerInfoObj != nullptr) && !InInfoLayerSettings.LayerInfoObj->bNoWeightBlend); },
		[](const FLandscapeInfoLayerSettings& InInfoLayerSettings) { return InInfoLayerSettings.LayerInfoObj->LayerName; });
	return WeightBlendedWeightmapLayerNames;
}

#endif // WITH_EDITOR