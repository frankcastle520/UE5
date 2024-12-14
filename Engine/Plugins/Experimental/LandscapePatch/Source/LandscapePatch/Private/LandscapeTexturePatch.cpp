// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTexturePatch.h"

#include "Engine/TextureRenderTarget2DArray.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEditResourcesSubsystem.h" // ULandscapeScratchRenderTarget
#include "LandscapePatchLogging.h"
#include "LandscapePatchManager.h"
#include "LandscapePatchUtil.h" // CopyTextureOnRenderThread
#include "LandscapeUtils.h" // IsVisibilityLayer
#include "MathUtil.h"
#include "RHIStaticStates.h"
#include "RenderGraphUtils.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "Algo/AnyOf.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LandscapeTexturePatch)

namespace LandscapeTexturePatchLocals
{
#if WITH_EDITOR
	template <typename TextureBackedRTType>
	void TransitionSourceMode(ELandscapeTexturePatchSourceMode OldMode, ELandscapeTexturePatchSourceMode NewMode,
		TObjectPtr<UTexture>& TextureAsset, TObjectPtr<TextureBackedRTType>& InternalData,
		TUniqueFunction<TextureBackedRTType* ()> InternalDataBuilder)
	{
		if (NewMode == ELandscapeTexturePatchSourceMode::None)
		{
			TextureAsset = nullptr;
			InternalData = nullptr;
		}
		else if (NewMode == ELandscapeTexturePatchSourceMode::TextureAsset)
		{
			InternalData = nullptr;
		}
		else // new mode is internal texture or render target
		{
			bool bWillUseTextureOnly = (NewMode == ELandscapeTexturePatchSourceMode::InternalTexture);
			bool bNeedToCopyTextureAsset = (OldMode == ELandscapeTexturePatchSourceMode::TextureAsset
				&& IsValid(TextureAsset) && TextureAsset->GetResource());

			if (!InternalData)
			{
				InternalData = InternalDataBuilder();
				InternalData->SetUseInternalTextureOnly(bWillUseTextureOnly && !bNeedToCopyTextureAsset);
				InternalData->Initialize();
			}
			else
			{
				InternalData->Modify();
			}

			InternalData->SetUseInternalTextureOnly(bWillUseTextureOnly && !bNeedToCopyTextureAsset);
			if (bNeedToCopyTextureAsset)
			{
				// Copy the currently set texture asset to our render target
				FTextureResource* Source = TextureAsset->GetResource();
				FTextureResource* Destination = InternalData->GetRenderTarget()->GetResource();

				ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatchRTToTexture)(
					[Source, Destination](FRHICommandListImmediate& RHICmdList)
					{
						UE::Landscape::PatchUtil::CopyTextureOnRenderThread(RHICmdList, *Source, *Destination);
					});
			}

			// Note that the duplicate SetUseInternalTextureOnly calls (in cases where we don't need to copy the texture asset)
			// are fine because they don't do anything.
			InternalData->SetUseInternalTextureOnly(bWillUseTextureOnly);

			TextureAsset = nullptr;
		}
	}

	// TODO: The way we currently do intialization is a bit of a hack in that we actually request to do
	//  a landscape update but we read instead of writing. In batched merge, this might not always work
	//  properly because a patch might be at the edge of a rendered batch, and thus only have part of it
	//  be initialized properly. The proper way to do reinitialization would be to use a special function
	//  to render the relevant part of the landscape directly to the patch. We should do this at some point,
	//  but it is not high priority because reinitialization does not currently seem to be commonly used.
	// @param PatchToHeightmapUVs This is expected to be a usual math matrix by this point, not Unreal's transposed one
	void DoReinitializationOverlapCheck(FMatrix44f& PatchToHeightmapUVs, int32 PatchTextureSizeX, int32 PatchTextureSizeY)
	{
		auto IsInsideHeightmap = [&PatchToHeightmapUVs](int32 X, int32 Y)->bool
		{
			float U = PatchToHeightmapUVs.M[0][0] * X + PatchToHeightmapUVs.M[0][1] * Y + PatchToHeightmapUVs.M[0][3];
			float V = PatchToHeightmapUVs.M[1][0] * X + PatchToHeightmapUVs.M[1][1] * Y + PatchToHeightmapUVs.M[1][3];

			return U >= 0 && U <= 1 && V >= 0 && V <= 1;
		};

		if (!IsInsideHeightmap(0, 0)
			|| !IsInsideHeightmap(0, PatchTextureSizeY-1)
			|| !IsInsideHeightmap(PatchTextureSizeX-1, 0)
			|| !IsInsideHeightmap(PatchTextureSizeX-1, PatchTextureSizeY-1))
		{
			UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: Part or all of the patch was outside "
				"a region of landscape being rendered. Reinitialization might not work be fully supported here."));
		}
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void ULandscapeTexturePatch::RenderLayer(FRenderParams& InRenderParams)
{
	using namespace UE::Landscape::PatchUtil;
	using namespace UE::Landscape::EditLayers;

	FTransform LandscapeHeightmapToWorld = GetHeightmapToWorld(InRenderParams.RenderAreaWorldTransform);

	ULandscapeScratchRenderTarget* LandscapeScratchRT = InRenderParams.MergeRenderContext->GetBlendRenderTargetWrite();

	const bool bIsHeightmapTarget = InRenderParams.MergeRenderContext->IsHeightmapMerge();
	if (bIsHeightmapTarget)
	{
		UTextureRenderTarget2D* CurrentData = LandscapeScratchRT->TryGetRenderTarget2D();
		if (!ensure(CurrentData))
		{
			return;
		}

		// graph builder expects external textures to start as SRV
		LandscapeScratchRT->TransitionTo(ERHIAccess::SRVMask);
		
		if (bReinitializeHeightOnNextRender)
		{
			bReinitializeHeightOnNextRender = false;
			ReinitializeHeight(CurrentData, LandscapeHeightmapToWorld);
			return;
		}
		else
		{
			ApplyToHeightmap(CurrentData, GetHeightmapToWorld(InRenderParams.RenderAreaWorldTransform));
			return;
		}
	}

	// If we got to here, we're dealing with weightmaps.

	UTextureRenderTarget2DArray* TextureArray = LandscapeScratchRT->TryGetRenderTarget2DArray();
	if (!ensure(TextureArray))
	{
		return;
	}
	
	// Only need to transition if we get a matching weight patch
	bool bTransitionedToSRV = false;

	int32 NumLayers = InRenderParams.RenderGroupTargetLayerNames.Num();
	if (!ensure(TextureArray->Slices == NumLayers))
	{
		NumLayers = FMath::Min(NumLayers, TextureArray->Slices);
	}

	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		bool bIsVisibilityLayer = ensure(LayerIndex < InRenderParams.RenderGroupTargetLayerInfos.Num()) 
			&& UE::Landscape::IsVisibilityLayer(InRenderParams.RenderGroupTargetLayerInfos[LayerIndex]);
		
		// Try to find the weight patch
		ULandscapeWeightPatchTextureInfo* WeightPatchInfo = nullptr;
		for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatchEntry : WeightPatches)
		{
			if ((bIsVisibilityLayer && WeightPatchEntry->bEditVisibilityLayer)
				|| (WeightPatchEntry->WeightmapLayerName == InRenderParams.RenderGroupTargetLayerNames[LayerIndex]))
			{
				WeightPatchInfo = WeightPatchEntry;
				break;
			}
		}

		if (!WeightPatchInfo)
		{
			// Didn't have a patch for this weight layer
			continue;
		}

		// graph builder expects external textures to start as SRV
		if (!bTransitionedToSRV)
		{
			LandscapeScratchRT->TransitionTo(ERHIAccess::SRVMask);
			bTransitionedToSRV = true;
		}

		if (WeightPatchInfo->bReinitializeOnNextRender)
		{
			WeightPatchInfo->bReinitializeOnNextRender = false;
			ReinitializeWeightPatch(WeightPatchInfo, TextureArray->GetResource(),
				FIntPoint(TextureArray->SizeX, TextureArray->SizeY), LayerIndex, LandscapeHeightmapToWorld);
		}
		else
		{
			ApplyToWeightmap(WeightPatchInfo, 
				[Resource = TextureArray->GetResource()]() { return Resource->GetTexture2DArrayRHI(); },
				LayerIndex, 
				InRenderParams.RenderAreaSectionRect.Size(), 
				GetHeightmapToWorld(InRenderParams.RenderAreaWorldTransform));
		}
	}//end for each layer index
}//end ULandscapeTexturePatch::RenderLayer

// Legacy path, which gets the entire heightmap.
UTextureRenderTarget2D* ULandscapeTexturePatch::RenderLayer_Native(const FLandscapeBrushParameters& InParameters, 
	const FTransform& LandscapeHeightmapToWorld)
{
	using namespace UE::Landscape;

	if (!IsPatchInWorld() || !IsEnabled())
	{
		return InParameters.CombinedResult;
	}

	const bool bIsHeightmapTarget = InParameters.LayerType == ELandscapeToolTargetType::Heightmap;
	const bool bIsWeightmapTarget = InParameters.LayerType == ELandscapeToolTargetType::Weightmap;
	const bool bIsVisibilityLayerTarget = InParameters.LayerType == ELandscapeToolTargetType::Visibility;

	if (bIsHeightmapTarget)
	{
		if (bReinitializeHeightOnNextRender)
		{
			bReinitializeHeightOnNextRender = false;
			ReinitializeHeight(InParameters.CombinedResult, LandscapeHeightmapToWorld);
			return InParameters.CombinedResult;
		}
		else
		{
			return ApplyToHeightmap(InParameters.CombinedResult, LandscapeHeightmapToWorld);
		}
	}
	else
	{
		// Try to find the weight patch
		ULandscapeWeightPatchTextureInfo* WeightPatchInfo = nullptr;

		for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatchEntry : WeightPatches)
		{
			if (!IsValid(WeightPatchEntry))
			{
				continue;
			}

			if ((bIsWeightmapTarget && (WeightPatchEntry->WeightmapLayerName == InParameters.WeightmapLayerName)) ||
				(bIsVisibilityLayerTarget && WeightPatchEntry->bEditVisibilityLayer))
			{
				WeightPatchInfo = WeightPatchEntry;
				break;
			}
		}

		if (!WeightPatchInfo)
		{
			return InParameters.CombinedResult;
		}

		if (WeightPatchInfo->bReinitializeOnNextRender)
		{
			WeightPatchInfo->bReinitializeOnNextRender = false;
			if (ensure(InParameters.CombinedResult->GetResource()))
			{
				ReinitializeWeightPatch(WeightPatchInfo, InParameters.CombinedResult->GetResource(), 
					FIntPoint(InParameters.CombinedResult->SizeX, InParameters.CombinedResult->SizeY), 
					-1, // Signifies that this is not a Texture2DArray
					LandscapeHeightmapToWorld);
			}
			return InParameters.CombinedResult;
		}
		else
		{
			ApplyToWeightmap(WeightPatchInfo, 
				[Resource = InParameters.CombinedResult->GetResource()]() { return Resource->GetTexture2DRHI(); },
				0, // Slice index
				FIntPoint(InParameters.CombinedResult->SizeX, InParameters.CombinedResult->SizeY),
				LandscapeHeightmapToWorld);
			return InParameters.CombinedResult;
		}
	}
}

UTextureRenderTarget2D* ULandscapeTexturePatch::ApplyToHeightmap(UTextureRenderTarget2D* InCombinedResult, 
	const FTransform& LandscapeHeightmapToWorld)
{
	using namespace UE::Landscape;

	// Get the source of our height patch
	UTexture* PatchUObject = nullptr;
	switch (HeightSourceMode)
	{
	case ELandscapeTexturePatchSourceMode::None:
		return InCombinedResult;
	case ELandscapeTexturePatchSourceMode::InternalTexture:
		PatchUObject = GetHeightInternalTexture();
		break;
	case ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget:
		PatchUObject = GetHeightRenderTarget(/*bMarkDirty = */ false);
		break;
	case ELandscapeTexturePatchSourceMode::TextureAsset:

		if (IsValid(HeightTextureAsset) && !ensureMsgf(HeightTextureAsset->VirtualTextureStreaming == 0,
			TEXT("ULandscapeTexturePatch: Virtual textures are not supported")))
		{
			return InCombinedResult;
		}
		PatchUObject = HeightTextureAsset;
		break;
	default:
		ensure(false);
	}

	if (!IsValid(PatchUObject))
	{
		return InCombinedResult;
	}

	FTextureResource* Patch = PatchUObject->GetResource();
	if (!Patch)
	{
		return InCombinedResult;
	}

	// Go ahead and pack everything into a copy of the param struct so we don't have to capture everything
	// individually in the lambda below.
	FApplyLandscapeTextureHeightPatchPS::FParameters ShaderParamsToCopy;
	FIntRect DestinationBounds;
	GetHeightShaderParams(LandscapeHeightmapToWorld, FIntPoint(Patch->GetSizeX(), Patch->GetSizeY()), FIntPoint(InCombinedResult->SizeX, InCombinedResult->SizeY), ShaderParamsToCopy, DestinationBounds);

	if (DestinationBounds.Area() <= 0)
	{
		// Patch must be outside the landscape.
		return InCombinedResult;
	}

	ENQUEUE_RENDER_COMMAND(LandscapeTextureHeightPatch)([InCombinedResult, ShaderParamsToCopy, Patch, DestinationBounds](FRHICommandListImmediate& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeTextureHeightPatch_Render);

			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("ApplyTextureHeightPatch"));

			TRefCountPtr<IPooledRenderTarget> DestinationRenderTarget = CreateRenderTarget(InCombinedResult->GetResource()->GetTexture2DRHI(), TEXT("LandscapeTextureHeightPatchOutput"));
			FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(DestinationRenderTarget);

			// Make a copy of the portion of our heightmap input that we're writing to so that we can 
			// read and write at the same time (needed for blending)
			FRDGTextureDesc InputCopyDescription = DestinationTexture->Desc;
			InputCopyDescription.NumMips = 1;
			InputCopyDescription.Extent = DestinationBounds.Size();
			FRDGTextureRef InputCopy = GraphBuilder.CreateTexture(InputCopyDescription, TEXT("LandscapeTextureHeightPatchInputCopy"));

			FRHICopyTextureInfo CopyTextureInfo;
			CopyTextureInfo.SourceMipIndex = 0;
			CopyTextureInfo.NumMips = 1;
			CopyTextureInfo.SourcePosition = FIntVector(DestinationBounds.Min.X, DestinationBounds.Min.Y, 0);
			CopyTextureInfo.Size = FIntVector(InputCopyDescription.Extent.X, InputCopyDescription.Extent.Y, 0);

			AddCopyTexturePass(GraphBuilder, DestinationTexture, InputCopy, CopyTextureInfo);

			FApplyLandscapeTextureHeightPatchPS::FParameters* ShaderParams =
				GraphBuilder.AllocParameters<FApplyLandscapeTextureHeightPatchPS::FParameters>();
			*ShaderParams = ShaderParamsToCopy;

			TRefCountPtr<IPooledRenderTarget> PatchRenderTarget = CreateRenderTarget(Patch->GetTexture2DRHI(), TEXT("LandscapeTextureHeightPatch"));
			FRDGTextureRef PatchTexture = GraphBuilder.RegisterExternalTexture(PatchRenderTarget);
			FRDGTextureSRVRef PatchSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(PatchTexture, 0));
			ShaderParams->InHeightPatch = PatchSRV;
			ShaderParams->InHeightPatchSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();

			FRDGTextureSRVRef InputCopySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(InputCopy, 0));
			ShaderParams->InSourceHeightmap = InputCopySRV;
			ShaderParams->InSourceHeightmapOffset = DestinationBounds.Min;

			ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);

			FApplyLandscapeTextureHeightPatchPS::AddToRenderGraph(GraphBuilder, ShaderParams, DestinationBounds);

			GraphBuilder.Execute();
		});

	return InCombinedResult;
}

void ULandscapeTexturePatch::ApplyToWeightmap(ULandscapeWeightPatchTextureInfo* PatchInfo, 
	TFunction<FRHITexture*()> RenderThreadLandscapeTextureGetter, int32 LandscapeTextureSliceIndex, 
	const FIntPoint& LandscapeTextureResolution, const FTransform& LandscapeHeightmapToWorld)
{
	using namespace UE::Landscape;

	if (!PatchInfo)
	{
		return;
	}

	UTexture* PatchUObject = nullptr;

	switch (PatchInfo->SourceMode)
	{
	case ELandscapeTexturePatchSourceMode::None:
		return;
	case ELandscapeTexturePatchSourceMode::InternalTexture:
		PatchUObject = GetWeightPatchInternalTexture(PatchInfo);
		break;
	case ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget:
		PatchUObject = GetWeightPatchRenderTarget(PatchInfo);
		break;
	case ELandscapeTexturePatchSourceMode::TextureAsset:
		if (IsValid(PatchInfo->TextureAsset) && !ensureMsgf(PatchInfo->TextureAsset->VirtualTextureStreaming == 0,
			TEXT("ULandscapeTexturePatch: Virtual textures are not supported")))
		{
			return;
		}
		PatchUObject = PatchInfo->TextureAsset;
		break;
	default:
		ensure(false);
	}

	if (!IsValid(PatchUObject))
	{
		return;
	}

	FTextureResource* Patch = PatchUObject->GetResource();
	if (!Patch)
	{
		return;
	}

	// Go ahead and pack everything into a copy of the param struct so we don't have to capture everything
	// individually in the lambda below.
	FApplyLandscapeTextureWeightPatchPS::FParameters ShaderParamsToCopy;
	FIntRect DestinationBounds;

	GetWeightShaderParams(LandscapeHeightmapToWorld, FIntPoint(Patch->GetSizeX(), Patch->GetSizeY()), 
		LandscapeTextureResolution, PatchInfo, ShaderParamsToCopy, DestinationBounds);

	if (DestinationBounds.Area() <= 0)
	{
		// Patch must be outside the landscape.
		return;
	}

	ENQUEUE_RENDER_COMMAND(LandscapeTextureWeightPatch)(
		[RenderThreadLandscapeTextureGetter, LandscapeTextureSliceIndex, ShaderParamsToCopy, Patch, DestinationBounds](FRHICommandListImmediate& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeTextureWeightPatch_Render);

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("ApplyTextureWeightPatch"));

		TRefCountPtr<IPooledRenderTarget> DestinationRenderTarget = CreateRenderTarget(
			RenderThreadLandscapeTextureGetter(), TEXT("LandscapeTextureWeightPatchOutput"));
		FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(DestinationRenderTarget);

		// Make a copy of the portion of our weightmap input that we're writing to so that we can 
		// read and write at the same time (needed for blending)
		FRDGTextureDesc InputCopyDescription = DestinationTexture->Desc;
		InputCopyDescription.Dimension = ETextureDimension::Texture2D;
		InputCopyDescription.ArraySize = 1;
		InputCopyDescription.NumMips = 1;
		InputCopyDescription.Extent = DestinationBounds.Size();
		FRDGTextureRef InputCopy = GraphBuilder.CreateTexture(InputCopyDescription, TEXT("LandscapeTextureWeightPatchInputCopy"));

		FRHICopyTextureInfo CopyTextureInfo;
		CopyTextureInfo.SourceMipIndex = 0;
		CopyTextureInfo.NumMips = 1;
		CopyTextureInfo.SourceSliceIndex = LandscapeTextureSliceIndex;
		CopyTextureInfo.NumSlices = 1;
		CopyTextureInfo.SourcePosition = FIntVector(DestinationBounds.Min.X, DestinationBounds.Min.Y, 0);
		CopyTextureInfo.Size = FIntVector(InputCopyDescription.Extent.X, InputCopyDescription.Extent.Y, 0);
		AddCopyTexturePass(GraphBuilder, DestinationTexture, InputCopy, CopyTextureInfo);

		FApplyLandscapeTextureWeightPatchPS::FParameters* ShaderParams =
			GraphBuilder.AllocParameters<FApplyLandscapeTextureWeightPatchPS::FParameters>();
		*ShaderParams = ShaderParamsToCopy;

		TRefCountPtr<IPooledRenderTarget> PatchRenderTarget = CreateRenderTarget(Patch->GetTexture2DRHI(), TEXT("LandscapeTextureWeightPatch"));
		FRDGTextureRef PatchTexture = GraphBuilder.RegisterExternalTexture(PatchRenderTarget);
		FRDGTextureSRVRef PatchSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(PatchTexture, 0));
		ShaderParams->InWeightPatch = PatchSRV;
		ShaderParams->InWeightPatchSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();

		FRDGTextureSRVRef InputCopySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(InputCopy, 0));
		ShaderParams->InSourceWeightmap = InputCopySRV;
		ShaderParams->InSourceWeightmapCoordOffset = DestinationBounds.Min;

		ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction,
			/*InMipIndex = */0, LandscapeTextureSliceIndex);

		FApplyLandscapeTextureWeightPatchPS::AddToRenderGraph(GraphBuilder, ShaderParams, DestinationBounds);

		GraphBuilder.Execute();
	});

}

void ULandscapeTexturePatch::GetCommonShaderParams(const FTransform& LandscapeHeightmapToWorldIn,
	const FIntPoint& SourceResolutionIn, const FIntPoint& DestinationResolutionIn, 
	FTransform& PatchToWorldOut, FVector2f& PatchWorldDimensionsOut, FMatrix44f& HeightmapToPatchOut, FIntRect& DestinationBoundsOut, 
	FVector2f& EdgeUVDeadBorderOut, float& FalloffWorldMarginOut) const
{
	PatchToWorldOut = GetPatchToWorldTransform();

	FVector2D FullPatchDimensions = GetFullUnscaledWorldSize();
	PatchWorldDimensionsOut = FVector2f(FullPatchDimensions);

	FTransform FromPatchUVToPatch(FQuat4d::Identity, FVector3d(-FullPatchDimensions.X / 2, -FullPatchDimensions.Y / 2, 0),
		FVector3d(FullPatchDimensions.X, FullPatchDimensions.Y, 1));
	FMatrix44d PatchLocalToUVs = FromPatchUVToPatch.ToInverseMatrixWithScale();

	FMatrix44d LandscapeToWorld = LandscapeHeightmapToWorldIn.ToMatrixWithScale();

	FMatrix44d WorldToPatch = PatchToWorldOut.ToInverseMatrixWithScale();

	// In unreal, matrix composition is done by multiplying the subsequent ones on the right, and the result
	// is transpose of what our shader will expect (because unreal right multiplies vectors by matrices).
	FMatrix44d LandscapeToPatchUVTransposed = LandscapeToWorld * WorldToPatch * PatchLocalToUVs;
	HeightmapToPatchOut = (FMatrix44f)LandscapeToPatchUVTransposed.GetTransposed();


	// Get the output bounds, which are used to limit the amount of landscape pixels we have to process. 
	// To get them, convert all of the corners into heightmap 2d coordinates and get the bounding box.
	auto PatchUVToHeightmap2DCoordinates = [&PatchToWorldOut, &FromPatchUVToPatch, &LandscapeHeightmapToWorldIn](const FVector2f& UV)
	{
		FVector WorldPosition = PatchToWorldOut.TransformPosition(
			FromPatchUVToPatch.TransformPosition(FVector(UV.X, UV.Y, 0)));
		FVector HeightmapCoordinates = LandscapeHeightmapToWorldIn.InverseTransformPosition(WorldPosition);
		return FVector2d(HeightmapCoordinates.X, HeightmapCoordinates.Y);
	};
	FBox2D FloatBounds(ForceInit);
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(0, 0));
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(0, 1));
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(1, 0));
	FloatBounds += PatchUVToHeightmap2DCoordinates(FVector2f(1, 1));

	DestinationBoundsOut = FIntRect(
		FMath::Clamp(FMath::Floor(FloatBounds.Min.X), 0, DestinationResolutionIn.X - 1),
		FMath::Clamp(FMath::Floor(FloatBounds.Min.Y), 0, DestinationResolutionIn.Y - 1),
		FMath::Clamp(FMath::CeilToInt(FloatBounds.Max.X) + 1, 0, DestinationResolutionIn.X),
		FMath::Clamp(FMath::CeilToInt(FloatBounds.Max.Y) + 1, 0, DestinationResolutionIn.Y));

	// The outer half-pixel shouldn't affect the landscape because it is not part of our official coverage area.
	EdgeUVDeadBorderOut = FVector2f::Zero();
	if (SourceResolutionIn.X * SourceResolutionIn.Y != 0)
	{
		EdgeUVDeadBorderOut = FVector2f(0.5 / SourceResolutionIn.X, 0.5 / SourceResolutionIn.Y);
	}

	FVector3d ComponentScale = PatchToWorldOut.GetScale3D();
	FalloffWorldMarginOut = Falloff / FMath::Min(ComponentScale.X, ComponentScale.Y);
}

void ULandscapeTexturePatch::GetHeightShaderParams(const FTransform& LandscapeHeightmapToWorldIn,
	const FIntPoint& SourceResolutionIn, const FIntPoint& DestinationResolutionIn,
	UE::Landscape::FApplyLandscapeTextureHeightPatchPS::FParameters& ParamsOut,
	FIntRect& DestinationBoundsOut) const
{
	using namespace UE::Landscape;

	FTransform PatchToWorld;
	GetCommonShaderParams(LandscapeHeightmapToWorldIn, SourceResolutionIn, DestinationResolutionIn,
		PatchToWorld, ParamsOut.InPatchWorldDimensions, ParamsOut.InHeightmapToPatch, 
		DestinationBoundsOut, ParamsOut.InEdgeUVDeadBorder, ParamsOut.InFalloffWorldMargin);

	FVector3d ComponentScale = PatchToWorld.GetScale3D();
	double LandscapeHeightScale = Landscape.IsValid() ? Landscape->GetTransform().GetScale3D().Z : 1;
	LandscapeHeightScale = LandscapeHeightScale == 0 ? 1 : LandscapeHeightScale;

	bool bNativeEncoding = HeightSourceMode == ELandscapeTexturePatchSourceMode::InternalTexture
		|| HeightEncoding == ELandscapeTextureHeightPatchEncoding::NativePackedHeight;

	// To get height scale in heightmap coordinates, we have to undo the scaling that happens to map the 16bit int to [-256, 256), and undo
	// the landscape actor scale.
	ParamsOut.InHeightScale = bNativeEncoding ? 1
		: LANDSCAPE_INV_ZSCALE * HeightEncodingSettings.WorldSpaceEncodingScale / LandscapeHeightScale;
	if (bApplyComponentZScale)
	{
		ParamsOut.InHeightScale *= ComponentScale.Z;
	}

	ParamsOut.InZeroInEncoding = bNativeEncoding ? LandscapeDataAccess::MidValue : HeightEncodingSettings.ZeroInEncoding;

	ParamsOut.InHeightOffset = 0;
	switch (ZeroHeightMeaning)
	{
	case ELandscapeTextureHeightPatchZeroHeightMeaning::LandscapeZ:
		break; // no offset necessary
	case ELandscapeTextureHeightPatchZeroHeightMeaning::PatchZ:
	{
		FVector3d PatchOriginInHeightmapCoords = LandscapeHeightmapToWorldIn.InverseTransformPosition(PatchToWorld.GetTranslation());
		ParamsOut.InHeightOffset = PatchOriginInHeightmapCoords.Z - LandscapeDataAccess::MidValue;
		break;
	}
	case ELandscapeTextureHeightPatchZeroHeightMeaning::WorldZero:
	{
		FVector3d WorldOriginInHeightmapCoords = LandscapeHeightmapToWorldIn.InverseTransformPosition(FVector::ZeroVector);
		ParamsOut.InHeightOffset = WorldOriginInHeightmapCoords.Z - LandscapeDataAccess::MidValue;
		break;
	}
	default:
		ensure(false);
	}

	ParamsOut.InBlendMode = static_cast<uint32>(BlendMode);

	// Pack our booleans into a bitfield
	using EShaderFlags = FApplyLandscapeTextureHeightPatchPS::EFlags;
	EShaderFlags Flags = EShaderFlags::None;

	Flags |= (FalloffMode == ELandscapeTexturePatchFalloffMode::RoundedRectangle) ?
		EShaderFlags::RectangularFalloff : EShaderFlags::None;

	Flags |= bUseTextureAlphaForHeight ?
		EShaderFlags::ApplyPatchAlpha : EShaderFlags::None;

	Flags |= bNativeEncoding ?
		EShaderFlags::InputIsPackedHeight : EShaderFlags::None;

	ParamsOut.InFlags = static_cast<uint8>(Flags);
}

void ULandscapeTexturePatch::GetWeightShaderParams(
	const FTransform& LandscapeHeightmapToWorldIn, const FIntPoint& SourceResolutionIn,
	const FIntPoint& DestinationResolutionIn, const ULandscapeWeightPatchTextureInfo* WeightPatchInfo, 
	UE::Landscape::FApplyLandscapeTextureWeightPatchPS::FParameters& ParamsOut, 
	FIntRect& DestinationBoundsOut) const
{
	using namespace UE::Landscape;

	FTransform PatchToWorld;
	GetCommonShaderParams(LandscapeHeightmapToWorldIn, SourceResolutionIn, DestinationResolutionIn,
		PatchToWorld, ParamsOut.InPatchWorldDimensions, ParamsOut.InWeightmapToPatch,
		DestinationBoundsOut, ParamsOut.InEdgeUVDeadBorder, ParamsOut.InFalloffWorldMargin);

	// Use the override blend mode if present, otherwise fall back to more general blend mode.
	ParamsOut.InBlendMode = static_cast<uint32>(WeightPatchInfo->bOverrideBlendMode ? WeightPatchInfo->OverrideBlendMode : BlendMode);

	// Pack our booleans into a bitfield
	using EShaderFlags = FApplyLandscapeTextureHeightPatchPS::EFlags;
	EShaderFlags Flags = EShaderFlags::None;

	Flags |= (FalloffMode == ELandscapeTexturePatchFalloffMode::RoundedRectangle) ?
		EShaderFlags::RectangularFalloff : EShaderFlags::None;

	Flags |= WeightPatchInfo->bUseAlphaChannel ?
		EShaderFlags::ApplyPatchAlpha : EShaderFlags::None;

	ParamsOut.InFlags = static_cast<uint8>(Flags);
}

// This function determines how our internal height render targets get converted to the format that gets
// serialized. In a perfect world, this largely shouldn't matter as long as we don't lose data in the conversion
// back and forth. In practice, it matters for transitioning the SourceMode between ELandscapeTexturePatchSourceMode::InternalTexture 
// and ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget, and it matters for reinitializing the patch
// from the current landscape. In the former, it matters because the transition is easy if the backing format
// is the same as the equivalent texture. In the latter, it matters because the reinitialization is easy if
// the backing format is the same as the applied landscape values. Currently we end up making the former easy, i.e.
// we serialize render targets to their equivalent native texture representation, and don't bake in the offset.
// This means that we need to do a bit more work when reinitializing to account for the offset.
// It should also be noted that there are some truncation/rounding implications to the choices made here that
// only matter if the user is messing around with the conversion parameters and hoping not to lose data... But
// there's a limited amount that we can protect the user in that case anyway.
FLandscapeHeightPatchConvertToNativeParams ULandscapeTexturePatch::GetHeightConvertToNativeParams() const
{
	// When doing conversions, we bake into a height in the same way that we do when applying the patch.

	FLandscapeHeightPatchConvertToNativeParams ConversionParams;
	ConversionParams.ZeroInEncoding = HeightEncodingSettings.ZeroInEncoding;

	double LandscapeHeightScale = Landscape.IsValid() ? Landscape->GetTransform().GetScale3D().Z : 1;
	LandscapeHeightScale = LandscapeHeightScale == 0 ? 1 : LandscapeHeightScale;
	ConversionParams.HeightScale = HeightEncodingSettings.WorldSpaceEncodingScale * LANDSCAPE_INV_ZSCALE / LandscapeHeightScale;

	// See above discussion about why we don't currently bake in height offset.
	ConversionParams.HeightOffset = 0;

	return ConversionParams;
}

#endif // WITH_EDITOR

void ULandscapeTexturePatch::RequestReinitializeHeight()
{
#if WITH_EDITOR
	if (!Super::IsEnabled())
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: Cannot reinitialize while disabled."));
		return;
	}

	if (!Landscape.IsValid())
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: No associated landscape to initialize from."));
		return;
	}

	if (!PatchManager.IsValid() && !GetBoundEditLayer())
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: Not bound to landscape (via edit layer)."));
		return;
	}

	FVector2D DesiredResolution(FMath::Max(1, InitTextureSizeX), FMath::Max(1, InitTextureSizeY));
	if (bBaseResolutionOffLandscape)
	{
		GetInitResolutionFromLandscape(ResolutionMultiplier, DesiredResolution);
	}
	SetResolution(DesiredResolution);

	bReinitializeHeightOnNextRender = true;
	RequestLandscapeUpdate();

#endif // WITH_EDITOR
}

void ULandscapeTexturePatch::RequestReinitializeWeights()
{
#if WITH_EDITOR
	if (!Super::IsEnabled())
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: Cannot reinitialize while disabled."));
		return;
	}

	if (!Landscape.IsValid())
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: No associated landscape to initialize from."));
		return;
	}

	if (!PatchManager.IsValid() && !GetBoundEditLayer())
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::Reinitialize: Not bound to landscape (via edit layer)."));
		return;
	}

	FVector2D DesiredResolution(FMath::Max(1, InitTextureSizeX), FMath::Max(1, InitTextureSizeY));
	if (bBaseResolutionOffLandscape)
	{
		GetInitResolutionFromLandscape(ResolutionMultiplier, DesiredResolution);
	}
	SetResolution(DesiredResolution);

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (Info)
	{
		for (const FLandscapeInfoLayerSettings& InfoLayerSettings : Info->Layers)
		{
			if (!InfoLayerSettings.LayerInfoObj)
			{
				continue;
			}
			
			FName WeightmapLayerName = InfoLayerSettings.GetLayerName();
			bool bIsVisibilityLayer = UE::Landscape::IsVisibilityLayer(InfoLayerSettings.LayerInfoObj);

			// Minor note: there's some undefined behavior if a user uses a patch that both has bEditVisibilityLayer
			//  set to true and a weight layer name that matches some other weight layer. That's ok.
			TArray<TObjectPtr<ULandscapeWeightPatchTextureInfo>> FoundPatches;
			if (bIsVisibilityLayer)
			{
				FoundPatches = WeightPatches.FilterByPredicate([](const TObjectPtr<ULandscapeWeightPatchTextureInfo>& PatchInfo)
				{
					return IsValid(PatchInfo) && PatchInfo->bEditVisibilityLayer;
				});
			}
			else
			{
				if (!ensure(WeightmapLayerName != NAME_None))
				{
					continue;
				}
				FoundPatches = WeightPatches.FilterByPredicate(
					[&WeightmapLayerName](const TObjectPtr<ULandscapeWeightPatchTextureInfo>& PatchInfo) 
				{ 
					return PatchInfo && PatchInfo->WeightmapLayerName == WeightmapLayerName; 
				});
			}

			if (FoundPatches.IsEmpty())
			{
				AddWeightPatch(WeightmapLayerName, ELandscapeTexturePatchSourceMode::InternalTexture, false);
				WeightPatches.Last()->bReinitializeOnNextRender = true;
				WeightPatches.Last()->bEditVisibilityLayer = bIsVisibilityLayer;
			}
			else
			{
				for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& PatchInfo : FoundPatches)
				{
					PatchInfo->bReinitializeOnNextRender = true;
				}
			}
		}
		RequestLandscapeUpdate();
	}

#endif // WITH_EDITOR
}

#if WITH_EDITOR
void ULandscapeTexturePatch::ReinitializeHeight(UTextureRenderTarget2D* InCombinedResult, const FTransform& LandscapeHeightmapToWorld)
{
	using namespace LandscapeTexturePatchLocals;

	if (HeightSourceMode == ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch: Cannot reinitialize height patch when source mode is an external texture."));
		return;
	}

	if (HeightSourceMode == ELandscapeTexturePatchSourceMode::None)
	{
		SetHeightSourceMode(ELandscapeTexturePatchSourceMode::InternalTexture);
	}
	else if (IsValid(HeightInternalData))
	{
		if (HeightSourceMode == ELandscapeTexturePatchSourceMode::InternalTexture && IsValid(HeightInternalData->GetInternalTexture()))
		{
			HeightInternalData->GetInternalTexture()->Modify();
		}
		else if (HeightSourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget && IsValid(HeightInternalData->GetRenderTarget()))
		{
			HeightInternalData->GetRenderTarget()->Modify();
		}
	}

	if (!ensure(IsValid(HeightInternalData)))
	{
		return;
	}

	SetUseAlphaChannelForHeight(false);
	SetBlendMode(ELandscapeTexturePatchBlendMode::AlphaBlend);
	ResetHeightRenderTargetFormat();

	// The way we're going to do it is that we'll copy the packed values directly to a temporary render target, offset 
	// them if needed (to undo whatever offsetting will happen during application), and store the result directly in the
	// backing internal texture. Then we'll update the actual associated render target from the internal texture (if needed) so
	// that unpacking and height format conversion happens the same way as everywhere else.

	// We do need to make sure that the scale conversion for the backing texture matches what will be used when applying it.
	UpdateHeightConvertToNativeParamsIfNeeded();

	UTextureRenderTarget2D* TemporaryNativeHeightCopy = NewObject<UTextureRenderTarget2D>(this);
	TemporaryNativeHeightCopy->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	TemporaryNativeHeightCopy->InitAutoFormat(ResolutionX, ResolutionY);
	TemporaryNativeHeightCopy->UpdateResourceImmediate(true);
	
	// If ZeroHeightMeaning is not landscape Z, then we're going to be applying an offset to our data when
	// applying it to landscape, which means we'll need to apply the inverse offset when initializing here
	// so that we get the same landscape back.
	double OffsetToApply = 0;
	if (ZeroHeightMeaning != ELandscapeTextureHeightPatchZeroHeightMeaning::LandscapeZ)
	{
		double ZeroHeight = 0;
		if (ZeroHeightMeaning == ELandscapeTextureHeightPatchZeroHeightMeaning::PatchZ)
		{
			ZeroHeight = LandscapeHeightmapToWorld.InverseTransformPosition(GetComponentTransform().GetTranslation()).Z;
		}
		else if (ZeroHeightMeaning == ELandscapeTextureHeightPatchZeroHeightMeaning::WorldZero)
		{
			ZeroHeight = LandscapeHeightmapToWorld.InverseTransformPosition(FVector::ZeroVector).Z;
		}
		OffsetToApply = LandscapeDataAccess::MidValue - ZeroHeight;
	}

	FMatrix44f PatchToSource = GetPatchToHeightmapUVs(LandscapeHeightmapToWorld, TemporaryNativeHeightCopy->SizeX, TemporaryNativeHeightCopy->SizeY, InCombinedResult->SizeX, InCombinedResult->SizeY);

	// TODO: see comment in function
	DoReinitializationOverlapCheck(PatchToSource, TemporaryNativeHeightCopy->SizeX, TemporaryNativeHeightCopy->SizeY);

	ENQUEUE_RENDER_COMMAND(LandscapeTexturePatchReinitializeHeight)(
		[Source = InCombinedResult->GetResource(), Destination = TemporaryNativeHeightCopy->GetResource(),
		&PatchToSource, OffsetToApply](FRHICommandListImmediate& RHICmdList)
	{
		using namespace UE::Landscape;

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("LandscapeTexturePatchReinitializeHeight"));

		FReinitializeLandscapePatchPS::FParameters* HeightmapResampleParams = GraphBuilder.AllocParameters<FReinitializeLandscapePatchPS::FParameters>();

		FRDGTextureRef HeightmapSource = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Source->GetTexture2DRHI(), TEXT("ReinitializationSource")));
		FRDGTextureSRVRef SourceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(HeightmapSource, 0));
		HeightmapResampleParams->InSource = SourceSRV;
		HeightmapResampleParams->InSourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
		HeightmapResampleParams->InPatchToSource = PatchToSource;

		FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Destination->GetTexture2DRHI(), TEXT("ReinitializationDestination")));

		if (OffsetToApply != 0)
		{
			FRDGTextureRef TemporaryDestination = GraphBuilder.CreateTexture(DestinationTexture->Desc, TEXT("LandscapeTextureHeightPatchInputCopy"));
			HeightmapResampleParams->RenderTargets[0] = FRenderTargetBinding(TemporaryDestination, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);

			FReinitializeLandscapePatchPS::AddToRenderGraph(GraphBuilder, HeightmapResampleParams, /*bHeightPatch*/ true);

			FOffsetHeightmapPS::FParameters* OffsetParams = GraphBuilder.AllocParameters<FOffsetHeightmapPS::FParameters>();

			FRDGTextureSRVRef InputSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(TemporaryDestination, 0));
			OffsetParams->InHeightmap = InputSRV;
			OffsetParams->InHeightOffset = OffsetToApply;
			OffsetParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);

			FOffsetHeightmapPS::AddToRenderGraph(GraphBuilder, OffsetParams);
		}
		else
		{
			HeightmapResampleParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);
			FReinitializeLandscapePatchPS::AddToRenderGraph(GraphBuilder, HeightmapResampleParams, /*bHeightPatch*/ true);
		}

		GraphBuilder.Execute();
	});

	// The Modify() calls currently don't really help because we don't transact inside Render_Native. Maybe someday
	// we'll add that ability (though it sounds messy).
	UTexture2D* InternalTexture = HeightInternalData->GetInternalTexture();
	InternalTexture->Modify();
	FText ErrorMessage;
	if (TemporaryNativeHeightCopy->UpdateTexture(InternalTexture, CTF_Default, /*InAlphaOverride = */nullptr, /*InTextureChangingDelegate =*/ [](UTexture*) {}, &ErrorMessage))
	{
		check(InternalTexture->Source.GetFormat() == ETextureSourceFormat::TSF_BGRA8);
		InternalTexture->UpdateResource();
	}
	else
	{
		UE_LOG(LogLandscapePatch, Error, TEXT("Couldn't copy heightmap render target to internal texture: %s"), *ErrorMessage.ToString());
	}
	InternalTexture->UpdateResource();

	if (IsValid(HeightInternalData->GetRenderTarget()))
	{
		HeightInternalData->GetRenderTarget()->Modify();
		HeightInternalData->CopyBackFromInternalTexture();
	}
}

void ULandscapeTexturePatch::ReinitializeWeightPatch(ULandscapeWeightPatchTextureInfo* PatchInfo, 
	FTextureResource* InputResource, FIntPoint ResourceSize, int32 SliceIndex, 
	const FTransform& LandscapeHeightmapToWorld)
{
	using namespace LandscapeTexturePatchLocals;

	if (!ensure(IsValid(PatchInfo) && InputResource))
	{
		return;
	}

	if (PatchInfo->SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		const FString LayerNameString = PatchInfo->WeightmapLayerName.ToString();
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch: Cannot initialize weight layer %s because source mode is an external texture."), *LayerNameString);
		return;
	}

	if (PatchInfo->SourceMode == ELandscapeTexturePatchSourceMode::None)
	{
		PatchInfo->SetSourceMode(ELandscapeTexturePatchSourceMode::InternalTexture);
	}
	else if (IsValid(PatchInfo->InternalData))
	{
		if (PatchInfo->SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture && IsValid(PatchInfo->InternalData->GetInternalTexture()))
		{
			PatchInfo->InternalData->GetInternalTexture()->Modify();
		}
		else if (PatchInfo->SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget && IsValid(PatchInfo->InternalData->GetRenderTarget()))
		{
			PatchInfo->InternalData->GetRenderTarget()->Modify();
		}
	}
	
	if (!ensure(PatchInfo->InternalData))
	{
		return;
	}

	PatchInfo->InternalData->SetUseAlphaChannel(false);
	if (BlendMode != ELandscapeTexturePatchBlendMode::AlphaBlend)
	{
		PatchInfo->bOverrideBlendMode = true;
		PatchInfo->OverrideBlendMode = ELandscapeTexturePatchBlendMode::AlphaBlend;
	}

	// We're going to copy directly to the associated render target. Make sure there is one for us to copy to.
	PatchInfo->InternalData->SetUseInternalTextureOnly(false, false);
	UTextureRenderTarget2D* RenderTarget = PatchInfo->InternalData->GetRenderTarget();
	if (!ensure(IsValid(RenderTarget)))
	{
		return;
	}

	FMatrix44f PatchToSource = GetPatchToHeightmapUVs(LandscapeHeightmapToWorld, 
		RenderTarget->SizeX, RenderTarget->SizeY, ResourceSize.X, ResourceSize.Y);

	// TODO: see comment in function
	DoReinitializationOverlapCheck(PatchToSource, RenderTarget->SizeX, RenderTarget->SizeY);

	ENQUEUE_RENDER_COMMAND(LandscapeTexturePatchReinitializeWeight)(
		[InputResource, SliceIndex, Destination = RenderTarget->GetResource(), &PatchToSource](FRHICommandListImmediate& RHICmdList)
	{
		using namespace UE::Landscape;

		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("LandscapeTexturePatchReinitializeWeight"));

		FReinitializeLandscapePatchPS::FParameters* ShaderParams = GraphBuilder.AllocParameters<FReinitializeLandscapePatchPS::FParameters>();

		if (SliceIndex < 0)
		{
			FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(
				InputResource->GetTexture2DRHI(), TEXT("ReinitializationSource")));
			ShaderParams->InSource = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));
		
		}
		else
		{
			FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(
				InputResource->GetTexture2DArrayRHI(), TEXT("ReinitializationSource")));
			FRDGTextureSRVDesc Desc = FRDGTextureSRVDesc::CreateForSlice(SourceTexture, SliceIndex);
			Desc.MipLevel = 0;
			Desc.NumMipLevels = 1;
			ShaderParams->InSource = GraphBuilder.CreateSRV(Desc);
		}

		ShaderParams->InSourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();

		ShaderParams->InPatchToSource = PatchToSource;

		FRDGTextureRef DestinationTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Destination->GetTexture2DRHI(), TEXT("ReinitializationDestination")));
		ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);
		FReinitializeLandscapePatchPS::AddToRenderGraph(GraphBuilder, ShaderParams, /*bHeightPatch*/ false);

		GraphBuilder.Execute();
	});

	PatchInfo->InternalData->SetUseInternalTextureOnly(PatchInfo->SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture, true);
}

FMatrix44f ULandscapeTexturePatch::GetPatchToHeightmapUVs(const FTransform& LandscapeHeightmapToWorld,
	int32 PatchSizeX, int32 PatchSizeY, int32 HeightmapSizeX, int32 HeightmapSizeY) const
{
	FVector2D FullPatchDimensions = GetFullUnscaledWorldSize();

	FTransform PatchPixelToPatchLocal(FQuat4d::Identity, FVector3d(-FullPatchDimensions.X / 2, -FullPatchDimensions.Y / 2, 0),
		FVector3d(FullPatchDimensions.X / PatchSizeX, FullPatchDimensions.Y / PatchSizeY, 1));

	FTransform PatchToWorld = GetPatchToWorldTransform();

	FTransform LandscapeUVToWorld = LandscapeHeightmapToWorld;
	LandscapeUVToWorld.MultiplyScale3D(FVector3d(HeightmapSizeX, HeightmapSizeY, 1));

	// In unreal, matrix composition is done by multiplying the subsequent ones on the right, and the result
	// is transpose of what our shader will expect (because unreal right multiplies vectors by matrices).
	FMatrix44d PatchToLandscapeUVTransposed = PatchPixelToPatchLocal.ToMatrixWithScale() * PatchToWorld.ToMatrixWithScale()
		* LandscapeUVToWorld.ToInverseMatrixWithScale();
	return (FMatrix44f)PatchToLandscapeUVTransposed.GetTransposed();
}

bool ULandscapeTexturePatch::CanAffectHeightmap() const
{
	return (HeightSourceMode != ELandscapeTexturePatchSourceMode::None
		// If source mode is texture asset, we need to have an asset to read from
		&& (HeightSourceMode != ELandscapeTexturePatchSourceMode::TextureAsset || HeightTextureAsset))
		// If reinitializing, we need to read from the render call
		|| bReinitializeHeightOnNextRender;
}

bool ULandscapeTexturePatch::CanAffectWeightmap() const
{
	return Algo::AnyOf(WeightPatches, [this](const TObjectPtr<ULandscapeWeightPatchTextureInfo>& InWeightPatch) 
	{ 
		return IsValid(InWeightPatch) && WeightPatchCanRender(*InWeightPatch);
	});
}

bool ULandscapeTexturePatch::CanAffectWeightmapLayer(const FName& InLayerName) const
{
	return Algo::AnyOf(WeightPatches, [InLayerName, this](TObjectPtr<ULandscapeWeightPatchTextureInfo> InWeightPatch) 
	{
		return IsValid(InWeightPatch) && (InWeightPatch->WeightmapLayerName == InLayerName)
			&& WeightPatchCanRender(*InWeightPatch);
	}); 
}

bool ULandscapeTexturePatch::CanAffectVisibilityLayer() const
{
	return Algo::AnyOf(WeightPatches, [this](const TObjectPtr<ULandscapeWeightPatchTextureInfo>& InWeightPatch) 
	{ 
		return IsValid(InWeightPatch) && InWeightPatch->bEditVisibilityLayer 
			&& WeightPatchCanRender(*InWeightPatch);
	});
}

bool ULandscapeTexturePatch::WeightPatchCanRender(const ULandscapeWeightPatchTextureInfo& InWeightPatch) const
{
	return (InWeightPatch.SourceMode != ELandscapeTexturePatchSourceMode::None 
		// If source mode is texture asset, we need to have an asset to read from
		&& (InWeightPatch.SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset || InWeightPatch.TextureAsset))
		// If reinitializing, we need to read from the render call
		|| InWeightPatch.bReinitializeOnNextRender;
}

void ULandscapeTexturePatch::GetRenderDependencies(TSet<UObject*>& OutDependencies) const
{
	Super::GetRenderDependencies(OutDependencies);

	if (HeightSourceMode == ELandscapeTexturePatchSourceMode::InternalTexture
		&& HeightInternalData && HeightInternalData->GetInternalTexture())
	{
		OutDependencies.Add(HeightInternalData->GetInternalTexture());
	}
	else if (HeightSourceMode == ELandscapeTexturePatchSourceMode::TextureAsset 
		&& HeightTextureAsset)
	{
		OutDependencies.Add(HeightTextureAsset);
	}
}

TStructOnScope<FActorComponentInstanceData> ULandscapeTexturePatch::GetComponentInstanceData() const
{
	// There are currently various issues with blueprints and instanced sub objects, and
	//  one of them causes undo to be severely broken for transactable instanced objects
	//  inside a blueprint actor component: UE-225445
	// As it happens, one workaround is to not have the objects be transactable. So for
	//  now, we temporarily make all instanced objects not transactable while doing instance
	//  data serialization (when it theoretically shouldn't matter anyway).

	auto SetObjectTransactionalFlag = [](UObject* Object, bool bOn)
	{
		if (!Object)
		{
			return;
		}
		if (bOn)
		{
			Object->SetFlags(RF_Transactional);
		}
		else
		{
			Object->ClearFlags(RF_Transactional);
		}
	};
	auto SetInternalDataTransactionalFlags = [&SetObjectTransactionalFlag](TObjectPtr<ULandscapeTextureBackedRenderTargetBase> InternalData, bool bOn)
	{
		if (!InternalData)
		{
			return;
		}
		SetObjectTransactionalFlag(InternalData, bOn);
		SetObjectTransactionalFlag(InternalData->GetRenderTarget(), bOn);
		SetObjectTransactionalFlag(InternalData->GetInternalTexture(), bOn);
	};
	auto SetAllInternalDataTransactionalFlags = [this, &SetObjectTransactionalFlag, &SetInternalDataTransactionalFlags](bool bOn)
	{
		SetInternalDataTransactionalFlags(HeightInternalData, bOn);
		for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
		{
			if (IsValid(WeightPatch))
			{
				SetObjectTransactionalFlag(WeightPatch, bOn);
				SetInternalDataTransactionalFlags(WeightPatch->InternalData, bOn);
			}
		}
	};
	
	SetAllInternalDataTransactionalFlags(false);
	TStructOnScope<FActorComponentInstanceData> ToReturn = Super::GetComponentInstanceData();
	SetAllInternalDataTransactionalFlags(true);

	return ToReturn;
}

#endif

void ULandscapeTexturePatch::SnapToLandscape()
{
#if WITH_EDITOR
	if (!Landscape.IsValid())
	{
		return;
	}

	Modify();

	FTransform LandscapeTransform = Landscape->GetTransform();
	FTransform PatchTransform = GetComponentTransform();

	FQuat LandscapeRotation = LandscapeTransform.GetRotation();
	FQuat PatchRotation = PatchTransform.GetRotation();

	// Get rotation of patch relative to landscape
	FQuat PatchRotationRelativeLandscape = LandscapeRotation.Inverse() * PatchRotation;

	// Get component of that relative rotation that is around the landscape Z axis.
	double RadiansAroundZ = PatchRotationRelativeLandscape.GetTwistAngle(FVector::ZAxisVector);

	// Round that rotation to nearest 90 degree increment
	int32 Num90DegreeRotations = FMath::RoundToDouble(RadiansAroundZ / FMathd::HalfPi);
	double NewRadiansAroundZ = Num90DegreeRotations * FMathd::HalfPi;

	// Now adjust the patch transform.
	FQuat NewPatchRotation = FQuat(FVector::ZAxisVector, NewRadiansAroundZ) * LandscapeRotation;
	SetWorldRotation(NewPatchRotation);

	// Once we have the rotation adjusted, we need to adjust the patch size and positioning.
	// However don't bother if either the patch or landscape scale is 0. We might still be able
	// to align in one of the axes in such a case, but it is not worth the code complexity for
	// a broken use case.
	FVector LandscapeScale = Landscape->GetTransform().GetScale3D();
	FVector PatchScale = GetComponentTransform().GetScale3D();
	if (LandscapeScale.X == 0 || LandscapeScale.Y == 0)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::SnapToLandscape: Landscape target "
			"for patch had a zero scale in one of the dimensions. Skipping aligning position."));
		return;
	}
	if (PatchScale.X == 0 || PatchScale.Y == 0)
	{
		UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::SnapToLandscape: Patch "
			"had a zero scale in one of the dimensions. Skipping aligning position."));
		return;
	}

	// Start by adjusting size to be a multiple of landscape quad size.
	double PatchExtentX = PatchScale.X * UnscaledPatchCoverage.X;
	double PatchExtentY = PatchScale.Y * UnscaledPatchCoverage.Y;
	if (Num90DegreeRotations % 2)
	{
		// Relative to the landscape, our lenght and width are backwards...
		Swap(PatchExtentX, PatchExtentY);
	}

	int32 LandscapeQuadsX = FMath::RoundToInt(PatchExtentX / LandscapeScale.X);
	int32 LandscapeQuadsY = FMath::RoundToInt(PatchExtentY / LandscapeScale.Y);

	double NewPatchExtentX = LandscapeQuadsX * LandscapeScale.X;
	double NewPatchExtentY = LandscapeQuadsY * LandscapeScale.Y;
	if (Num90DegreeRotations % 2)
	{
		Swap(NewPatchExtentX, NewPatchExtentY);
	}
	UnscaledPatchCoverage = FVector2D(NewPatchExtentX / PatchScale.X, NewPatchExtentY / PatchScale.Y);

	// Now adjust the center of the patch. This gets snapped to either integer or integer + 0.5 increments
	// in landscape coordinates depending on whether patch length/width is odd or even in landscape coordinates.

	FVector PatchCenterInLandscapeCoordinates = LandscapeTransform.InverseTransformPosition(GetComponentLocation());
	double NewPatchCenterX, NewPatchCenterY;
	if (LandscapeQuadsX % 2)
	{
		NewPatchCenterX = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.X + 0.5) - 0.5;
	}
	else
	{
		NewPatchCenterX = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.X);
	}
	if (LandscapeQuadsY % 2)
	{
		NewPatchCenterY = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.Y + 0.5) - 0.5;
	}
	else
	{
		NewPatchCenterY = FMath::RoundToDouble(PatchCenterInLandscapeCoordinates.Y);
	}

	FVector NewCenterInLandscape(NewPatchCenterX, NewPatchCenterY, PatchCenterInLandscapeCoordinates.Z);
	SetWorldLocation(LandscapeTransform.TransformPosition(NewCenterInLandscape));
	RequestLandscapeUpdate();
#endif // WITH_EDITOR
}

void ULandscapeTexturePatch::SetResolution(FVector2D ResolutionIn)
{
	int32 DesiredX = FMath::Max(1, ResolutionIn.X);
	int32 DesiredY = FMath::Max(1, ResolutionIn.Y);

	if (DesiredX == ResolutionX && DesiredY == ResolutionY)
	{
		return;
	}
	Modify();

	ResolutionX = DesiredX;
	ResolutionY = DesiredY;
	InitTextureSizeX = ResolutionX;
	InitTextureSizeY = ResolutionY;

	auto ResizePatch = [DesiredX, DesiredY](ELandscapeTexturePatchSourceMode SourceMode, ULandscapeTextureBackedRenderTargetBase* InternalData)
	{
		// Deal with height first
		if (SourceMode == ELandscapeTexturePatchSourceMode::TextureAsset || SourceMode == ELandscapeTexturePatchSourceMode::None)
		{
			return;
		}
		else if (ensure(IsValid(InternalData)))
		{
			InternalData->SetResolution(DesiredX, DesiredY);
		}
	};

	ResizePatch(HeightSourceMode, HeightInternalData);
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch))
		{
			ResizePatch(WeightPatch->SourceMode, WeightPatch->InternalData);
		}
	}
}

FVector2D ULandscapeTexturePatch::GetFullUnscaledWorldSize() const
{
	FVector2D Resolution = GetResolution();

	// UnscaledPatchCoverage is meant to represent the distance between the centers of the extremal pixels.
	// That distance in pixels is Resolution-1.
	FVector2D TargetPixelSize(UnscaledPatchCoverage / FVector2D::Max(Resolution - 1, FVector2D(1, 1)));
	return TargetPixelSize * Resolution;
}

FTransform ULandscapeTexturePatch::GetPatchToWorldTransform() const
{
	FTransform PatchToWorld = GetComponentTransform();

	if (Landscape.IsValid())
	{
		FRotator3d PatchRotator = PatchToWorld.GetRotation().Rotator();
		FRotator3d LandscapeRotator = Landscape->GetTransform().GetRotation().Rotator();
		PatchToWorld.SetRotation(FRotator3d(LandscapeRotator.Pitch, PatchRotator.Yaw, LandscapeRotator.Roll).Quaternion());
	}

	return PatchToWorld;
}

bool ULandscapeTexturePatch::GetInitResolutionFromLandscape(float ResolutionMultiplierIn, FVector2D& ResolutionOut) const
{
	if (!Landscape.IsValid())
	{
		return false;
	}

	ResolutionOut = FVector2D::One();

	FVector LandscapeScale = Landscape->GetTransform().GetScale3D();
	// We go off of the larger dimension so that our patch works in different rotations.
	double LandscapeQuadSize = FMath::Max(FMath::Abs(LandscapeScale.X), FMath::Abs(LandscapeScale.Y));

	if (LandscapeQuadSize > 0)
	{
		double PatchQuadSize = LandscapeQuadSize;
		PatchQuadSize /= (ResolutionMultiplierIn > 0 ? ResolutionMultiplierIn : 1);

		FVector PatchScale = GetComponentTransform().GetScale3D();
		double NumQuadsX = FMath::Abs(UnscaledPatchCoverage.X * PatchScale.X / PatchQuadSize);
		double NumQuadsY = FMath::Abs(UnscaledPatchCoverage.Y * PatchScale.Y / PatchQuadSize);

		ResolutionOut = FVector2D(
			FMath::Max(1, FMath::CeilToInt(NumQuadsX) + 1),
			FMath::Max(1, FMath::CeilToInt(NumQuadsY) + 1)
		);

		return true;
	}
	return false;
}

#if WITH_EDITOR
void ULandscapeTexturePatch::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	using namespace LandscapeTexturePatchLocals;

	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeTexturePatch, DetailPanelHeightSourceMode))
		{
			// When changing source mode in the detail panel to a render target, we need to know the format to use, particularly 
			// whether we need an alpha channel
			if ((DetailPanelHeightSourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget
					// This also affects an internal texture if we're copying from a texture asset, because we copy through render target
					|| DetailPanelHeightSourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
				// However we don't want to touch the format if we started with a render target source mode, because that would clear
				// the render target before we can copy it to an internal texture (if that's what we're switching to).
				&& HeightSourceMode != ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
			{
				ResetHeightRenderTargetFormat();
			}
			SetHeightSourceMode(DetailPanelHeightSourceMode);
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeTexturePatch, HeightEncoding))
		{
			ResetHeightEncodingMode(HeightEncoding);
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeTexturePatch, WeightPatches))
		{
			// In certain cases, changes to the internals of a weight info object trigger a PostEditChangeProperty
			//  on the patch instead of the info object. For instance this happens when editing the objects in the
			//  blueprint editor and propagating the change to an instance (something that frequently does not work
			//  due to propagation being unreliable for this array, see comment on WeightPatches).
			for (TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
			{
				if (IsValid(WeightPatch))
				{
					WeightPatch->SetSourceMode(WeightPatch->DetailPanelSourceMode);
				}
			}
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FLandscapeTexturePatchEncodingSettings, ZeroInEncoding)
			|| PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FLandscapeTexturePatchEncodingSettings, WorldSpaceEncodingScale))
		{
			UpdateHeightConvertToNativeParamsIfNeeded();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULandscapeWeightPatchTextureInfo::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	using namespace LandscapeTexturePatchLocals;

	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeWeightPatchTextureInfo, DetailPanelSourceMode)
			&& DetailPanelSourceMode != SourceMode)
		{
			SetSourceMode(DetailPanelSourceMode);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULandscapeWeightPatchTextureInfo::PreDuplicate(FObjectDuplicationParameters& DupParams)
{
	// TODO: It seems like this whole overload shouldn't be necessary, because we should get PreDuplicate calls
	// on InternalData. However for reasons that I have yet to undertand, those calls are not made. It seems like
	// there is different behavior for an array of instanced classes containing instanced properties...

	Super::PreDuplicate(DupParams);

	if (SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget && InternalData)
	{
		InternalData->CopyToInternalTexture();
	}
}
#endif // WITH_EDITOR

void ULandscapeWeightPatchTextureInfo::SetSourceMode(ELandscapeTexturePatchSourceMode NewMode)
{
#if WITH_EDITOR

	if (SourceMode == NewMode)
	{
		return;
	}
	Modify();

	if (!IsTemplate())
	{
		TransitionSourceModeInternal(SourceMode, NewMode);
	}
	// In a template, it is not safe to try to allocate a texture, etc. All we do is clear out the
	// texture asset pointer if it is not needed, to avoid referencing assets unnecessarily.
	else if (SourceMode != ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		TextureAsset = nullptr;
	}

	SourceMode = NewMode;
	DetailPanelSourceMode = NewMode;
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void ULandscapeWeightPatchTextureInfo::TransitionSourceModeInternal(ELandscapeTexturePatchSourceMode OldMode, ELandscapeTexturePatchSourceMode NewMode)
{
	using namespace LandscapeTexturePatchLocals;

	FVector2D Resolution(1, 1);
	if (ULandscapeTexturePatch* OwningPatch = Cast<ULandscapeTexturePatch>(GetOuter()))
	{
		Resolution = OwningPatch->GetResolution();
	}

	TransitionSourceMode<ULandscapeWeightTextureBackedRenderTarget>(SourceMode, NewMode, TextureAsset, InternalData, [&Resolution, this]()
	{
		ULandscapeWeightTextureBackedRenderTarget* InternalDataToReturn = NewObject<ULandscapeWeightTextureBackedRenderTarget>(this);
		InternalDataToReturn->SetFlags(RF_Transactional);
		InternalDataToReturn->SetResolution(Resolution.X, Resolution.Y);
		return InternalDataToReturn;
	});
}
#endif

void ULandscapeTexturePatch::SetHeightSourceMode(ELandscapeTexturePatchSourceMode NewMode)
{
#if WITH_EDITOR
	using namespace LandscapeTexturePatchLocals;

	if (HeightSourceMode == NewMode)
	{
		return;
	}
	Modify();

	if (!IsTemplate())
	{
		TransitionHeightSourceModeInternal(HeightSourceMode, NewMode);
	}
	// In a template, it is not safe to try to allocate a texture, etc. All we do is clear out the
	// texture asset pointer if it is not needed, to avoid referencing assets unnecessarily.
	else if (HeightSourceMode != ELandscapeTexturePatchSourceMode::TextureAsset)
	{
		HeightTextureAsset = nullptr;
	}

	HeightSourceMode = NewMode;
	DetailPanelHeightSourceMode = NewMode;
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void ULandscapeTexturePatch::TransitionHeightSourceModeInternal(ELandscapeTexturePatchSourceMode OldMode, ELandscapeTexturePatchSourceMode NewMode)
{
	using namespace LandscapeTexturePatchLocals;

	TransitionSourceMode<ULandscapeHeightTextureBackedRenderTarget>(HeightSourceMode, NewMode, HeightTextureAsset, HeightInternalData, [this]()
	{
		ULandscapeHeightTextureBackedRenderTarget* InternalDataToReturn = NewObject<ULandscapeHeightTextureBackedRenderTarget>(this);
		InternalDataToReturn->SetFlags(RF_Transactional);
		InternalDataToReturn->SetResolution(ResolutionX, ResolutionY);
		InternalDataToReturn->SetFormat(HeightRenderTargetFormat);
		InternalDataToReturn->ConversionParams = GetHeightConvertToNativeParams();

		return InternalDataToReturn;
	});
}
#endif

void ULandscapeTexturePatch::SetHeightTextureAsset(UTexture* TextureIn)
{
	ensureMsgf(!TextureIn || TextureIn->VirtualTextureStreaming == 0,
		TEXT("ULandscapeTexturePatch::SetHeightTextureAsset: Virtual textures are not supported."));
	HeightTextureAsset = TextureIn;
}

UTextureRenderTarget2D* ULandscapeTexturePatch::GetHeightRenderTarget(bool bMarkDirty)
{
#if WITH_EDITOR

	if (IsTemplate())
	{
		return nullptr;
	}

	if (bMarkDirty)
	{
		MarkPackageDirty();
	}

	// In templates (i.e. in blueprint editor), it's not safe to create textures, so if we are an instantiation
	//  of a blueprint, we may not yet have the internal render target allocated. It might seem like a good idea
	//  to do this in OnComponentCreated, but that causes default construction script instance data application
	//  to see the data as modified, and prevents it from being carried over properly (see usage of GetUCSModifiedProperties
	//  in ComponentInstanceDataCache.cpp). Doing it in ApplyComponentInstanceData also seems to be a good idea at
	//  first, but we can't do it in ECacheApplyPhase::PostSimpleConstructionScript for the same reason as OnComponentModified,
	//  and doing it in ECacheApplyPhase::PostUserConstructionScript is too late because the user may want to write
	//  to the render target in the user construction script.
	// So, we do this allocation right when the render target is requested.
	if (HeightSourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		if (!HeightInternalData || !HeightInternalData->GetRenderTarget())
		{
			TransitionHeightSourceModeInternal(ELandscapeTexturePatchSourceMode::None, HeightSourceMode);
		}

		return ensure(HeightInternalData) ? HeightInternalData->GetRenderTarget() : nullptr;
	}
#endif

	return nullptr;
}

UTexture2D* ULandscapeTexturePatch::GetHeightInternalTexture()
{
#if WITH_EDITOR

	if (IsTemplate())
	{
		return nullptr;
	}

	// Allocate data if needed (see comment in GetHeightRenderTarget)
	if (HeightSourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget
		|| HeightSourceMode == ELandscapeTexturePatchSourceMode::InternalTexture)
	{
		if (!HeightInternalData || !HeightInternalData->GetInternalTexture())
		{
			TransitionHeightSourceModeInternal(ELandscapeTexturePatchSourceMode::None, HeightSourceMode);
		}

		return ensure(HeightInternalData) ? HeightInternalData->GetInternalTexture() : nullptr;
	}
#endif

	return nullptr;
}

void ULandscapeTexturePatch::UpdateHeightConvertToNativeParamsIfNeeded()
{
#if WITH_EDITOR
	if (HeightInternalData)
	{
		FLandscapeHeightPatchConvertToNativeParams ConversionParams = GetHeightConvertToNativeParams();
		if (ConversionParams.HeightScale == 0)
		{
			// If the scale is 0, then storing in the texture would lose the data we have,
			// so keep whatever the previous storage encoding was if nonzero, otherwise set to 1.
			ConversionParams.HeightScale = HeightInternalData->ConversionParams.HeightScale != 0 ? HeightInternalData->ConversionParams.HeightScale
				: 1;
		}
		
		if (ConversionParams.ZeroInEncoding != HeightInternalData->ConversionParams.ZeroInEncoding
			|| ConversionParams.HeightScale != HeightInternalData->ConversionParams.HeightScale
			|| ConversionParams.HeightOffset != HeightInternalData->ConversionParams.HeightOffset)
		{
			HeightInternalData->Modify();
			HeightInternalData->ConversionParams = ConversionParams;
		}
	}
#endif
}

void ULandscapeTexturePatch::ResetHeightEncodingMode(ELandscapeTextureHeightPatchEncoding EncodingMode)
{
#if WITH_EDITOR
	Modify();
	HeightEncoding = EncodingMode;
	if (EncodingMode == ELandscapeTextureHeightPatchEncoding::ZeroToOne)
	{
		HeightEncodingSettings.ZeroInEncoding = 0.5;
		HeightEncodingSettings.WorldSpaceEncodingScale = 400;
	}
	else if (EncodingMode == ELandscapeTextureHeightPatchEncoding::WorldUnits)
	{
		HeightEncodingSettings.ZeroInEncoding = 0;
		HeightEncodingSettings.WorldSpaceEncodingScale = 1;
	}
	ResetHeightRenderTargetFormat();

	UpdateHeightConvertToNativeParamsIfNeeded();
#endif
}

#if WITH_EDITOR
void ULandscapeTexturePatch::ResetHeightRenderTargetFormat()
{
	SetHeightRenderTargetFormat(HeightEncoding == ELandscapeTextureHeightPatchEncoding::NativePackedHeight ? ETextureRenderTargetFormat::RTF_RGBA8
		: bUseTextureAlphaForHeight ? ETextureRenderTargetFormat::RTF_RGBA32f : ETextureRenderTargetFormat::RTF_R32f);
}
#endif

void ULandscapeTexturePatch::SetHeightEncodingSettings(const FLandscapeTexturePatchEncodingSettings& Settings)
{
	Modify();
	HeightEncodingSettings = Settings;

	UpdateHeightConvertToNativeParamsIfNeeded();
}

void ULandscapeTexturePatch::SetHeightRenderTargetFormat(ETextureRenderTargetFormat Format)
{
	if (HeightRenderTargetFormat == Format)
	{
		return;
	}

	Modify();
	HeightRenderTargetFormat = Format;
	if (HeightInternalData)
	{
		HeightInternalData->SetFormat(HeightRenderTargetFormat);
	}
}


void ULandscapeTexturePatch::AddWeightPatch(const FName& WeightmapLayerName, ELandscapeTexturePatchSourceMode SourceMode, bool bUseAlphaChannel)
{
#if WITH_EDITOR
	using namespace LandscapeTexturePatchLocals;

	// Try to modify an existing entry instead if possible
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (!IsValid(WeightPatch))
		{
			continue;
		}

		if (WeightPatch->WeightmapLayerName == WeightmapLayerName)
		{
			if (WeightPatch->SourceMode != SourceMode)
			{
				WeightPatch->SetSourceMode(SourceMode);
			}
			if (IsValid(WeightPatch->InternalData))
			{
				WeightPatch->InternalData->SetUseAlphaChannel(bUseAlphaChannel);
			}
			return;
		}
	}

	// The object creation is modeled after SPropertyEditorEditInline::OnClassPicked, which is how these are created
	// from the detail panel. We probably don't need the archetype check, admittedly, but might as well keep it.
	EObjectFlags NewObjectFlags = GetMaskedFlags(RF_PropagateToSubObjects);
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		NewObjectFlags |= RF_ArchetypeObject;
	}
	ULandscapeWeightPatchTextureInfo* NewWeightPatch = NewObject<ULandscapeWeightPatchTextureInfo>(this, NAME_None, NewObjectFlags);

	NewWeightPatch->WeightmapLayerName = WeightmapLayerName;
	NewWeightPatch->SourceMode = SourceMode;
	NewWeightPatch->DetailPanelSourceMode = SourceMode;
	NewWeightPatch->bUseAlphaChannel = bUseAlphaChannel;

	if (NewWeightPatch->SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture
		|| NewWeightPatch->SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		NewWeightPatch->InternalData = NewObject<ULandscapeWeightTextureBackedRenderTarget>(NewWeightPatch);
		NewWeightPatch->InternalData->SetFlags(RF_Transactional);
		NewWeightPatch->InternalData->SetResolution(ResolutionX, ResolutionY);
		NewWeightPatch->InternalData->SetUseAlphaChannel(bUseAlphaChannel);
		NewWeightPatch->InternalData->Initialize();
	}

	WeightPatches.Add(NewWeightPatch);
#endif // WITH_EDITOR
}

void ULandscapeTexturePatch::RemoveWeightPatch(const FName& InWeightmapLayerName)
{
	WeightPatches.RemoveAll([InWeightmapLayerName](const TObjectPtr<ULandscapeWeightPatchTextureInfo>& PatchInfo)
	{ 
		return PatchInfo && PatchInfo->WeightmapLayerName == InWeightmapLayerName; 
	});
}

void ULandscapeTexturePatch::RemoveAllWeightPatches()
{
	WeightPatches.Reset();
}

void ULandscapeTexturePatch::DisableAllWeightPatches()
{
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch))
		{
			WeightPatch->SetSourceMode(ELandscapeTexturePatchSourceMode::None);
		}
	}
}

TArray<FName> ULandscapeTexturePatch::GetAllWeightPatchLayerNames()
{
	TArray<FName> Names;
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName != NAME_None)
		{
			Names.AddUnique(WeightPatch->WeightmapLayerName);
		}
	}

	return Names;
}

void ULandscapeTexturePatch::SetUseAlphaChannelForWeightPatch(const FName& InWeightmapLayerName, bool bUseAlphaChannel)
{
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			WeightPatch->bUseAlphaChannel = bUseAlphaChannel;
			if (WeightPatch->InternalData)
			{
				WeightPatch->InternalData->SetUseAlphaChannel(bUseAlphaChannel);
			}
			return;
		}
	}
	const FString LayerNameString = InWeightmapLayerName.ToString();
	UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::SetUseAlphaChannelForWeightPatch: Unable to find data for weight layer %s"), *LayerNameString);
}

void ULandscapeTexturePatch::SetWeightPatchSourceMode(const FName& InWeightmapLayerName, ELandscapeTexturePatchSourceMode NewMode)
{
#if WITH_EDITOR
	using namespace LandscapeTexturePatchLocals;

	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			WeightPatch->SetSourceMode(NewMode);
			return;
		}
	}
	const FString LayerNameString = InWeightmapLayerName.ToString();
	UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::SetWeightPatchSourceMode: Unable to find data for weight layer %s"), *LayerNameString);
#endif // WITH_EDITOR
}

ELandscapeTexturePatchSourceMode ULandscapeTexturePatch::GetWeightPatchSourceMode(const FName& InWeightmapLayerName)
{
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			return WeightPatch->SourceMode;
		}
	}
	return ELandscapeTexturePatchSourceMode::None;
}

UTextureRenderTarget2D* ULandscapeTexturePatch::GetWeightPatchRenderTarget(const FName& InWeightmapLayerName, bool bMarkDirty)
{
	if (IsTemplate())
	{
		return nullptr;
	}

	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			if (bMarkDirty)
			{
				MarkPackageDirty();
			}

			return GetWeightPatchRenderTarget(WeightPatch);
		}
	}
	return nullptr;
}

UTextureRenderTarget2D* ULandscapeTexturePatch::GetWeightPatchRenderTarget(ULandscapeWeightPatchTextureInfo* WeightPatch)
{
#if WITH_EDITOR
	if (IsTemplate() || !IsValid(WeightPatch))
	{
		return nullptr;
	}

	// Allocate data if needed (see comment in GetHeightRenderTarget)
	if (WeightPatch->SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		if (!WeightPatch->InternalData || !WeightPatch->InternalData->GetRenderTarget())
		{
			WeightPatch->TransitionSourceModeInternal(ELandscapeTexturePatchSourceMode::None, WeightPatch->SourceMode);
		}

		return ensure(WeightPatch->InternalData) ? WeightPatch->InternalData->GetRenderTarget() : nullptr;
	}
#endif

	return nullptr;
}

UTexture2D* ULandscapeTexturePatch::GetWeightPatchInternalTexture(ULandscapeWeightPatchTextureInfo* WeightPatch)
{
#if WITH_EDITOR
	if (IsTemplate() || !IsValid(WeightPatch))
	{
		return nullptr;
	}

	// Allocate data if needed (see comment in GetHeightRenderTarget)
	if (WeightPatch->SourceMode == ELandscapeTexturePatchSourceMode::InternalTexture 
		|| WeightPatch->SourceMode == ELandscapeTexturePatchSourceMode::TextureBackedRenderTarget)
	{
		if (!WeightPatch->InternalData || !WeightPatch->InternalData->GetInternalTexture())
		{
			WeightPatch->TransitionSourceModeInternal(ELandscapeTexturePatchSourceMode::None, WeightPatch->SourceMode);
		}

		return ensure(WeightPatch->InternalData) ? WeightPatch->InternalData->GetInternalTexture() : nullptr;
	}
#endif

	return nullptr;
}

void ULandscapeTexturePatch::SetWeightPatchTextureAsset(const FName& InWeightmapLayerName, UTexture* TextureIn)
{
	if (!ensureMsgf(!TextureIn || TextureIn->VirtualTextureStreaming == 0,
		TEXT("ULandscapeTexturePatch::SetWeightPatchTextureAsset: Virtual textures are not supported.")))
	{
		return;
	}

	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			WeightPatch->TextureAsset = TextureIn;
			return;
		}
	}

	const FString LayerNameString = InWeightmapLayerName.ToString();
	UE_LOG(LogLandscapePatch, Warning, TEXT("ULandscapeTexturePatch::SetWeightPatchTextureAsset: Unable to find data for weight layer %s"), *LayerNameString);
}

void ULandscapeTexturePatch::SetWeightPatchBlendModeOverride(const FName& InWeightmapLayerName, ELandscapeTexturePatchBlendMode BlendModeIn)
{
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			WeightPatch->OverrideBlendMode = BlendModeIn;
			WeightPatch->bOverrideBlendMode = true;
			return;
		}
	}
}

void ULandscapeTexturePatch::ClearWeightPatchBlendModeOverride(const FName& InWeightmapLayerName)
{
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			WeightPatch->bOverrideBlendMode = false;
			return;
		}
	}
}

void ULandscapeTexturePatch::SetEditVisibilityLayer(const FName& InWeightmapLayerName, const bool bEditVisibilityLayer)
{
	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatch->WeightmapLayerName == InWeightmapLayerName)
		{
			WeightPatch->bEditVisibilityLayer = bEditVisibilityLayer;
		}
	}
}

#if WITH_EDITOR
void ULandscapeTexturePatch::GetRendererStateInfo(const ULandscapeInfo* InLandscapeInfo,
	FEditLayerTargetTypeState& OutSupported, FEditLayerTargetTypeState& OutEnabled,
	TArray<TSet<FName>>& OutRenderGroups) const
{
	if (CanAffectHeightmap())
	{
		OutSupported.AddTargetType(ELandscapeToolTargetType::Heightmap);
	}

	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatchCanRender(*WeightPatch))
		{
			if (WeightPatch->bEditVisibilityLayer)
			{
				OutSupported.AddTargetType(ELandscapeToolTargetType::Visibility);
			}
			else
			{
				OutSupported.AddTargetType(ELandscapeToolTargetType::Weightmap);
				OutSupported.AddWeightmap(WeightPatch->WeightmapLayerName);
				OutRenderGroups.Add(TSet<FName>({ WeightPatch->WeightmapLayerName }));
			}
		}
	}

	if (IsEnabled())
	{
		OutEnabled = OutSupported;
	}
}

FString ULandscapeTexturePatch::GetEditLayerRendererDebugName() const
{
	return FString::Printf(TEXT("%s:%s"), *GetOwner()->GetActorNameOrLabel(), *GetName());
}

TArray<UE::Landscape::EditLayers::FEditLayerRenderItem> ULandscapeTexturePatch::GetRenderItems(const ULandscapeInfo* InLandscapeInfo) const
{
	using namespace UE::Landscape::EditLayers;

	TArray<FEditLayerRenderItem> AffectedAreas;

	FTransform ComponentTransform = this->GetComponentToWorld();
	FOOBox2D PatchArea(ComponentTransform, GetFullUnscaledWorldSize());
	FInputWorldArea InputWorldArea = FInputWorldArea::CreateOOBox(PatchArea);
	FOutputWorldArea OutputWorldArea = FOutputWorldArea::CreateOOBox(PatchArea);

	if (CanAffectHeightmap())
	{
		FEditLayerTargetTypeState TargetInfo(ELandscapeToolTargetTypeFlags::Heightmap);
		FEditLayerRenderItem Item(TargetInfo, InputWorldArea, OutputWorldArea, false);
		AffectedAreas.Add(Item);
	}

	for (const TObjectPtr<ULandscapeWeightPatchTextureInfo>& WeightPatch : WeightPatches)
	{
		if (IsValid(WeightPatch) && WeightPatchCanRender(*WeightPatch))
		{
			FEditLayerTargetTypeState TargetInfo = WeightPatch->bEditVisibilityLayer ? 
				FEditLayerTargetTypeState(ELandscapeToolTargetTypeFlags::Visibility)
				: FEditLayerTargetTypeState(ELandscapeToolTargetTypeFlags::Weightmap, { WeightPatch->WeightmapLayerName });
			FEditLayerRenderItem Item(TargetInfo, InputWorldArea, OutputWorldArea, false);
			AffectedAreas.Add(Item);
		}
	}

	return AffectedAreas;
}
#endif // WITH_EDITOR
