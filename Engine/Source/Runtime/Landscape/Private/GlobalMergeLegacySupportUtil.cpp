// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlobalMergeLegacySupportUtil.h"

#include "LandscapeBlueprintBrushBase.h" // FLandscapeBrushParameters
#include "LandscapeEditTypes.h" // ELandscapeToolTargetType
#include "Engine/TextureRenderTarget2D.h"

#if WITH_EDITOR
UTextureRenderTarget2D* ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport::RenderAsBlueprintBrush(
	const FLandscapeBrushParameters& InParameters, const FTransform& NewLandscapeTransform)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ILandscapeBrushRenderCallAdapter_GlobalMergeLegacySupport::RenderAsBlueprintBrush);

	// Do the same early outs and processing as rendering a blueprint brush

	if (((InParameters.LayerType == ELandscapeToolTargetType::Heightmap) && !AffectsHeightmapAsBlueprintBrush())
		|| ((InParameters.LayerType == ELandscapeToolTargetType::Weightmap) && !AffectsWeightmapLayerAsBlueprintBrush(InParameters.WeightmapLayerName))
		|| ((InParameters.LayerType == ELandscapeToolTargetType::Visibility) && !AffectsVisibilityLayerAsBlueprintBrush()))
	{
		return nullptr;
	}

	if (InParameters.CombinedResult == nullptr)
	{
		return nullptr;
	}

	const FIntPoint NewLandscapeRenderTargetSize = FIntPoint(InParameters.CombinedResult->SizeX, InParameters.CombinedResult->SizeY);
	if (!CurrentRenderAreaWorldTransform.Equals(NewLandscapeTransform) 
		|| (CurrentRenderAreaSize != InParameters.RenderAreaSize)
		|| CurrentRenderTargetSize != NewLandscapeRenderTargetSize)
	{
		CurrentRenderAreaWorldTransform = NewLandscapeTransform;
		CurrentRenderAreaSize = InParameters.RenderAreaSize;
		CurrentRenderTargetSize = NewLandscapeRenderTargetSize;

		InitializeAsBlueprintBrush(CurrentRenderAreaWorldTransform, CurrentRenderAreaSize, CurrentRenderTargetSize);
	}

	UTextureRenderTarget2D* Result = RenderLayerAsBlueprintBrush(InParameters);

	return Result;
}
#endif