// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeCircleHeightPatch.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTarget2DArray.h"
#include "Landscape.h"
#include "LandscapeCircleHeightPatchPS.h"
#include "LandscapeEditResourcesSubsystem.h" // ULandscapeScratchRenderTarget
#include "LandscapeInfo.h"
#include "LandscapePatchUtil.h" // GetHeightmapToWorld
#include "LandscapeUtils.h" // IsVisibilityLayer
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h" // AddCopyTexturePass
#include "RenderingThread.h"
#include "TextureResource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LandscapeCircleHeightPatch)

#define LOCTEXT_NAMESPACE "LandscapeCircleHeightPatch"

void ULandscapeCircleHeightPatch::OnComponentCreated()
{
	Super::OnComponentCreated();

	// If we haven't been made from a copy, initialize the radius and transform of the patch
	// based on our parent.
	if (!bWasCopy)
	{
		AActor* ParentActor = GetAttachParentActor();
		if (ParentActor)
		{
			FVector Origin, BoxExtent;
			GetAttachParentActor()->GetActorBounds(false, Origin, BoxExtent);

			// Place the component at the bottom of the bounding box.
			Origin.Z -= BoxExtent.Z;
			SetWorldLocation(Origin);

			Radius = FMath::Sqrt(BoxExtent.X * BoxExtent.X + BoxExtent.Y * BoxExtent.Y);
			Falloff = Radius / 2;
		}
	}
}

UTextureRenderTarget2D* ULandscapeCircleHeightPatch::RenderLayer_Native(const FLandscapeBrushParameters& InParameters, const FTransform& HeightmapCoordsToWorld)
{
	// Circle height patch doesn't affect regular weightmap layers.
	if ((bEditVisibility && (InParameters.LayerType != ELandscapeToolTargetType::Visibility)) || (!bEditVisibility && (InParameters.LayerType != ELandscapeToolTargetType::Heightmap)))
	{
		return InParameters.CombinedResult;
	}

	ApplyCirclePatch(InParameters.LayerType == ELandscapeToolTargetType::Visibility,
		[CombinedResult = InParameters.CombinedResult] { return CombinedResult->GetResource()->GetTexture2DRHI(); }, 0, 
		FIntPoint(InParameters.CombinedResult->SizeX, InParameters.CombinedResult->SizeY),
		HeightmapCoordsToWorld);

	return InParameters.CombinedResult;
}

#if WITH_EDITOR
void ULandscapeCircleHeightPatch::RenderLayer(FRenderParams& InRenderParams)
{
	using namespace UE::Landscape::PatchUtil;
	using namespace UE::Landscape::EditLayers;

	FTransform HeightmapCoordsToWorld = GetHeightmapToWorld(InRenderParams.RenderAreaWorldTransform);

	ULandscapeScratchRenderTarget* LandscapeRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetWrite();

	const bool bIsHeightmapTarget = InRenderParams.MergeRenderContext->IsHeightmapMerge();
	if (bIsHeightmapTarget)
	{
		UTextureRenderTarget2D* OutputToBlendInto = LandscapeRT->TryGetRenderTarget2D();
		if (ensure(OutputToBlendInto))
		{
			// graph builder expects external textures to start as SRV
			LandscapeRT->TransitionTo(ERHIAccess::SRVMask); 

			ApplyCirclePatch(false, [OutputToBlendInto] { return OutputToBlendInto->GetResource()->GetTexture2DRHI(); },
				0, InRenderParams.RenderAreaSectionRect.Size(), HeightmapCoordsToWorld);
		}
		return;
	}

	// If we got to here, we're not processing a heightmap, so we only need to do anything if the 
	//  patch edits visibility.
	if (!bEditVisibility)
	{
		return;
	}

	UTextureRenderTarget2DArray* TextureArray = LandscapeRT->TryGetRenderTarget2DArray();
	if (!ensure(TextureArray))
	{
		return;
	}

	int32 NumLayers = InRenderParams.RenderGroupTargetLayerInfos.Num();
	if (!ensure(TextureArray->Slices == NumLayers))
	{
		NumLayers = FMath::Min(NumLayers, TextureArray->Slices);
	}

	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		if (UE::Landscape::IsVisibilityLayer(InRenderParams.RenderGroupTargetLayerInfos[LayerIndex]))
		{
			// graph builder expects external textures to start as SRV
			LandscapeRT->TransitionTo(ERHIAccess::SRVMask);

			ApplyCirclePatch(true, [TextureArray] { return TextureArray->GetResource()->GetTexture2DArrayRHI(); }, 
				LayerIndex, InRenderParams.RenderAreaSectionRect.Size(), HeightmapCoordsToWorld);
		}
	}
}
#endif // WITH_EDITOR

void ULandscapeCircleHeightPatch::ApplyCirclePatch(bool bIsVisibilityLayer,
	const TFunction<FRHITexture* ()>& RenderThreadLandscapeTextureGetter, int32 LandscapeTextureSliceIndex,
	const FIntPoint& DestinationResolution, const FTransform & HeightmapCoordsToWorld)
{
	if (bEditVisibility != bIsVisibilityLayer)
	{
		return;
	}

	double ToHeightmapRadiusScale = GetComponentTransform().GetScale3D().X / HeightmapCoordsToWorld.GetScale3D().X;
	FVector3d CircleCenterWorld = GetComponentTransform().GetTranslation();
	FVector3d CenterInHeightmapCoordinates = HeightmapCoordsToWorld.InverseTransformPosition(CircleCenterWorld);
	float RadiusAdjustment = bExclusiveRadius ? 0 : 1;
	float HeightmapRadius = Radius * ToHeightmapRadiusScale + RadiusAdjustment;
	// TODO: This is incorrect, should not have radius adjustment here. However, need to change in a separate CL
	//  so that we can add a fixup to leave older assets unchanged.
	float HeightmapFalloff = Falloff * ToHeightmapRadiusScale + RadiusAdjustment;

	FIntRect DestinationBounds(
		FMath::Clamp(FMath::Floor(CenterInHeightmapCoordinates.X - HeightmapRadius - HeightmapFalloff), 
			0, DestinationResolution.X),
		FMath::Clamp(FMath::Floor(CenterInHeightmapCoordinates.Y - HeightmapRadius - HeightmapFalloff), 
			0, DestinationResolution.Y),
		FMath::Clamp(FMath::CeilToInt(CenterInHeightmapCoordinates.X + HeightmapRadius + HeightmapFalloff) + 1, 
			0, DestinationResolution.X),
		FMath::Clamp(FMath::CeilToInt(CenterInHeightmapCoordinates.Y + HeightmapRadius + HeightmapFalloff) + 1, 
			0, DestinationResolution.Y));

	if (DestinationBounds.Area() <= 0)
	{
		// Must be outside the landscape
		return;
	}

	ENQUEUE_RENDER_COMMAND(LandscapeCircleHeightPatch)([RenderThreadLandscapeTextureGetter, LandscapeTextureSliceIndex, DestinationBounds,
		CenterInHeightmapCoordinates, HeightmapRadius, HeightmapFalloff, bEditVisibility = bEditVisibility](FRHICommandListImmediate& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeCircleHeightPatch);

		const TCHAR* OutputName = bEditVisibility ? TEXT("LandscapeCircleVisibilityPatchOutput") : TEXT("LandscapeCircleHeightPatchOutput");
		const TCHAR* InputCopyName = bEditVisibility ? TEXT("LandscapeCircleVisibilityPatchInputCopy") : TEXT("LandscapeCircleHeightPatchInputCopy");

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("ApplyLandscapeCirclePatch"));
		
		TRefCountPtr<IPooledRenderTarget> RenderTarget = CreateRenderTarget(RenderThreadLandscapeTextureGetter(), OutputName);
		FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(RenderTarget);

		// Make a copy of the portion of our weightmap input that we're writing to so that we can 
		//  read and write at the same time (needed for blending)
		FRDGTextureDesc InputCopyDescription = DestinationTexture->Desc;
		InputCopyDescription.Dimension = ETextureDimension::Texture2D;
		InputCopyDescription.ArraySize = 1;
		InputCopyDescription.NumMips = 1;
		InputCopyDescription.Extent = DestinationBounds.Size();
		FRDGTextureRef InputCopy = GraphBuilder.CreateTexture(InputCopyDescription, InputCopyName);

		FRHICopyTextureInfo CopyTextureInfo;
		CopyTextureInfo.SourceMipIndex = 0;
		CopyTextureInfo.NumMips = 1;
		CopyTextureInfo.SourceSliceIndex = LandscapeTextureSliceIndex;
		CopyTextureInfo.NumSlices = 1;
		CopyTextureInfo.SourcePosition = FIntVector(DestinationBounds.Min.X, DestinationBounds.Min.Y, 0);
		CopyTextureInfo.Size = FIntVector(InputCopyDescription.Extent.X, InputCopyDescription.Extent.Y, 0);
		AddCopyTexturePass(GraphBuilder, DestinationTexture, InputCopy, CopyTextureInfo);

		FRDGTextureSRVRef InputCopySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(InputCopy, 0));

		FLandscapeCircleHeightPatchPS::FParameters* ShaderParams = GraphBuilder.AllocParameters<FLandscapeCircleHeightPatchPS::FParameters>();
		ShaderParams->InCenter = (FVector3f)CenterInHeightmapCoordinates;
		ShaderParams->InRadius = HeightmapRadius;
		ShaderParams->InFalloff = HeightmapFalloff;
		ShaderParams->InSourceTexture = InputCopySRV;
		ShaderParams->InSourceTextureOffset = DestinationBounds.Min;
		ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, 
			/*InMipIndex = */0, LandscapeTextureSliceIndex);

		if (bEditVisibility)
		{
			FLandscapeCircleVisibilityPatchPS::AddToRenderGraph(GraphBuilder, ShaderParams, DestinationBounds);
		}
		else
		{
			FLandscapeCircleHeightPatchPS::AddToRenderGraph(GraphBuilder, ShaderParams, DestinationBounds);
		}

		GraphBuilder.Execute();
	});
}

#if WITH_EDITOR
void ULandscapeCircleHeightPatch::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
	FEditLayerTargetTypeState& OutSupported,
	FEditLayerTargetTypeState& OutEnabled,
	TArray<TSet<FName>>& OutRenderGroups) const
{
	OutSupported.AddTargetType(bEditVisibility ? ELandscapeToolTargetType::Visibility
		: ELandscapeToolTargetType::Heightmap);

	if (IsEnabled())
	{
		OutEnabled = OutSupported;
	}
}

FString ULandscapeCircleHeightPatch::GetEditLayerRendererDebugName() const
{
	return FString::Printf(TEXT("%s:%s"), *GetOwner()->GetActorNameOrLabel(), *GetName());
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ULandscapeCircleHeightPatch::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	TArray<FEditLayerRenderItem> AffectedAreas;

	FTransform ComponentTransform = this->GetComponentToWorld();

	// Figure out the extents of the patch. It will be radius + falloff + an adjustment if we're
	//  trying to make the whole circle lie flat. The adjustment will be the size of one landscape
	//  quad, but to be safe we'll make it two quads in each direction.
	FVector3d LandscapeScale = InLandscapeInfo->LandscapeActor.IsValid() ?
		InLandscapeInfo->LandscapeActor->GetActorTransform().GetScale3D() : FVector3d::Zero();
	FVector2D Extents(2 * FMath::Max(LandscapeScale.X, LandscapeScale.Y) + Radius + Falloff);

	FOOBox2D PatchArea(ComponentTransform, Extents);

	FInputWorldArea InputWorldArea = FInputWorldArea::CreateOOBox(PatchArea);
	FOutputWorldArea OutputWorldArea = FOutputWorldArea::CreateOOBox(PatchArea);

	FEditLayerTargetTypeState TargetInfo(bEditVisibility ? ELandscapeToolTargetTypeFlags::Visibility
		: ELandscapeToolTargetTypeFlags::Heightmap);
	FEditLayerRenderItem Item(TargetInfo, InputWorldArea, OutputWorldArea,
		// ModifyExistingWeightmapsOnly: we want the patch to allocate the visibility layer if it needs to, in its region.
		false);
	AffectedAreas.Add(Item);

	return AffectedAreas;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
