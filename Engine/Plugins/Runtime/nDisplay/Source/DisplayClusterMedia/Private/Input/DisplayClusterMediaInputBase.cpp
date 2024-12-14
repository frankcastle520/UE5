// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/DisplayClusterMediaInputBase.h"

#include "DisplayClusterMediaHelpers.h"
#include "DisplayClusterMediaLog.h"

#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "TextureResource.h"
#include "RHIUtilities.h"

#include "OpenColorIOColorSpace.h"
#include "OpenColorIORendering.h"

#include "RenderGraphBuilder.h"
#include "ScreenPass.h"

#include "Engine/Engine.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"


TAutoConsoleVariable<bool> CVarTempRivermaxCropWorkaround(
	TEXT("nDisplay.Media.Rivermax.CropWorkaround"),
	true,
	TEXT("nDisplay workaround for Rivermax input\n")
	TEXT("0 : Disabled\n")
	TEXT("1 : Enabled\n"),
	ECVF_RenderThreadSafe
);

// Based on the discussion, it looks like the problem is the incoming 2110 textures
// may have up to 3 ExtraPixelsThreshold extra pixels.
TAutoConsoleVariable<int32> CVarTempRivermaxExtraPixelsThreshold(
	TEXT("nDisplay.Media.Rivermax.ExtraPixelsThreshold"),
	3,
	TEXT("nDisplay workaround for Rivermax input\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarTempRivermaxExtraPixelsRemove(
	TEXT("nDisplay.Media.Rivermax.ExtraPixelsRemove"),
	0,
	TEXT("nDisplay workaround for Rivermax input\n"),
	ECVF_RenderThreadSafe
);

FDisplayClusterMediaInputBase::FDisplayClusterMediaInputBase(
	const FString& InMediaId,
	const FString& InClusterNodeId,
	UMediaSource* InMediaSource,
	bool bInLateOCIO
)
	: FDisplayClusterMediaBase(InMediaId, InClusterNodeId, bInLateOCIO)
{
	checkSlow(InMediaSource);
	MediaSource = DuplicateObject(InMediaSource, GetTransientPackage());
	checkSlow(MediaSource);

	// Instantiate media player
	MediaPlayer = NewObject<UMediaPlayer>();
	if (MediaPlayer)
	{
		MediaPlayer->SetLooping(false);
		MediaPlayer->PlayOnOpen = false;

		// Instantiate media texture
		MediaTexture = NewObject<UMediaTexture>();
		if (MediaTexture)
		{
			MediaTexture->NewStyleOutput = true;
			MediaTexture->SetRenderMode(UMediaTexture::ERenderMode::JustInTime);
			MediaTexture->SetMediaPlayer(MediaPlayer);
			MediaTexture->UpdateResource();
		}
	}
}


void FDisplayClusterMediaInputBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MediaSource);
	Collector.AddReferencedObject(MediaPlayer);
	Collector.AddReferencedObject(MediaTexture);
}

bool FDisplayClusterMediaInputBase::Play()
{
	if (MediaSource && MediaPlayer && MediaTexture)
	{
		MediaPlayer->PlayOnOpen = true;
		MediaPlayer->OnMediaEvent().AddRaw(this, &FDisplayClusterMediaInputBase::OnMediaEvent);

		bWasPlayerStarted = MediaPlayer->OpenSource(MediaSource);

		static FName RiverMaxPlayerName(TEXT("RivermaxMedia"));
		bRunningRivermaxMedia = MediaPlayer->GetPlayerName() == RiverMaxPlayerName;

		return bWasPlayerStarted;
	}

	return false;
}

void FDisplayClusterMediaInputBase::Stop()
{
	if (MediaPlayer)
	{
		bWasPlayerStarted = false;
		MediaPlayer->Close();
		MediaPlayer->OnMediaEvent().RemoveAll(this);
	}

	// Release internals
	ReleaseInternals();

	bRunningRivermaxMedia = false;
}

void FDisplayClusterMediaInputBase::OverrideTextureRegions_RenderThread(FIntRect& InOutSrcRect, FIntRect& InOutDstRect) const
{
	const FIntPoint SrcSize = InOutSrcRect.Size();
	const FIntPoint DstSize = InOutDstRect.Size();
	if (SrcSize == DstSize)
	{
		return;
	}

	// [Workaround]
	// Based on the discussion, it looks like the problem is the incoming 2110 textures
	// may have up to 3 ExtraPixelsThreshold extra pixels.
	// If this is the only difference, we just copy the required subregion.
	if (bRunningRivermaxMedia && CVarTempRivermaxCropWorkaround.GetValueOnRenderThread())
	{
		const int32 ExtraPixelsThreshold = CVarTempRivermaxExtraPixelsThreshold.GetValueOnRenderThread();

		// Crop if required
		if (SrcSize.Y == DstSize.Y
			&& SrcSize.X >= DstSize.X
			&& (SrcSize.X - DstSize.X) <= ExtraPixelsThreshold)
		{
			// Use Dest size
			InOutSrcRect.Max.X = InOutSrcRect.Min.X + DstSize.X;

			return;
		}

		// By default we always remove extra pixels from the right side.
		const int32 ExtraPixelsRemove = CVarTempRivermaxExtraPixelsRemove.GetValueOnRenderThread();

		InOutSrcRect.Max.X -= ExtraPixelsRemove;
	}
}

void FDisplayClusterMediaInputBase::ReleaseInternals()
{
	OCIOAppliedTexture.SafeRelease();
}

void FDisplayClusterMediaInputBase::ImportMediaData_RenderThread(FRHICommandListImmediate& RHICmdList, const FMediaInputTextureInfo& TextureInfo)
{
	UE_LOG(LogDisplayClusterMedia, Verbose, TEXT("MediaInput '%s': importing texture on RT frame '%llu'..."), *GetMediaId(), GFrameCounterRenderThread);

	MediaTexture->JustInTimeRender();

	FRHITexture* SrcTexture = MediaTexture->GetResource() ? MediaTexture->GetResource()->GetTextureRHI() : nullptr;
	FRHITexture* const DstTexture = TextureInfo.Texture;

	if (!SrcTexture || !DstTexture)
	{
		return;
	}

	// Apply OCIO if needed
	if (IsLateOCIO())
	{
		const bool bApplied = ProcessLateOCIO(RHICmdList, SrcTexture, TextureInfo.OCIOPassResources);
		if (bApplied)
		{
			// Redirect SrcTexture to the intermediate OCIO texture so we'll be importing this new one
			SrcTexture = OCIOAppliedTexture;
		}
	}

	if (SrcTexture && DstTexture)
	{
		FIntRect SrcRect(FIntPoint(0, 0), SrcTexture->GetDesc().Extent);
		FIntRect DstRect(TextureInfo.Region);
		OverrideTextureRegions_RenderThread(SrcRect, DstRect);

		const bool bSrcSrgb = EnumHasAnyFlags(SrcTexture->GetFlags(), TexCreate_SRGB);
		const bool bDstSrgb = EnumHasAnyFlags(DstTexture->GetFlags(), TexCreate_SRGB);

		if ((SrcTexture->GetDesc().Format == DstTexture->GetDesc().Format)
			&& (SrcRect.Size() == DstRect.Size())
			&& (bSrcSrgb == bDstSrgb)
			)
		{
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.SourcePosition = FIntVector(SrcRect.Min.X, SrcRect.Min.Y, 0);
			CopyInfo.DestPosition = FIntVector(DstRect.Min.X, DstRect.Min.Y, 0);
			CopyInfo.Size = FIntVector(DstRect.Size().X, DstRect.Size().Y, 0);

			TransitionAndCopyTexture(RHICmdList, SrcTexture, DstTexture, CopyInfo);
		}
		else
		{
			DisplayClusterMediaHelpers::ResampleTexture_RenderThread(RHICmdList, SrcTexture, DstTexture, SrcRect, DstRect);
		}
	}
}

bool FDisplayClusterMediaInputBase::ProcessLateOCIO(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, const FOpenColorIORenderPassResources& OCIORenderPassResources)
{
	checkSlow(SrcTexture);

	if (!SrcTexture)
	{
		return false;
	}

	if (!OCIORenderPassResources.IsValid())
	{
		return false;
	}

	// Create an intermediate texture if not exists
	if (!OCIOAppliedTexture)
	{
		OCIOAppliedTexture = CreateTexture(SrcTexture);
	}
	// Re-create it if parameters don't match
	else
	{
		const bool bSameFormat = (OCIOAppliedTexture->GetDesc().Format == SrcTexture->GetDesc().Format);
		const bool bSameSize   = (OCIOAppliedTexture->GetDesc().Extent == SrcTexture->GetDesc().Extent);

		if (!bSameFormat || !bSameSize)
		{
			OCIOAppliedTexture = CreateTexture(SrcTexture);
		}
	}

	if (!OCIOAppliedTexture)
	{
		return false;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef InputTexture = RegisterExternalTexture(GraphBuilder, SrcTexture, TEXT("DCMediaLateOCIOTexIn"));
	FRDGTextureRef OutputTexture = RegisterExternalTexture(GraphBuilder, OCIOAppliedTexture, TEXT("DCMediaLateOCIOTexOut"));

	const FIntPoint OutputResolution = OCIOAppliedTexture->GetDesc().Extent;
	const FIntRect  OutputRect = FIntRect(FIntPoint::ZeroValue, OutputResolution);

	FScreenPassTexture Input = FScreenPassTexture(InputTexture);
	FScreenPassRenderTarget Output = FScreenPassRenderTarget(OutputTexture, OutputRect, ERenderTargetLoadAction::EClear);

	FOpenColorIORendering::AddPass_RenderThread(
		GraphBuilder,
		FScreenPassViewInfo(),
		GEngine->GetDefaultWorldFeatureLevel(),
		Input,
		Output,
		OCIORenderPassResources,
		1.0f,
		EOpenColorIOTransformAlpha::None
	);

	GraphBuilder.Execute();

	return true;
}

FTextureRHIRef FDisplayClusterMediaInputBase::CreateTexture(const FRHITexture* ReferenceTexture)
{
	if (!ReferenceTexture)
	{
		return nullptr;
	}

	// Use original format and size
	const int32 SizeX = ReferenceTexture->GetDesc().Extent.X;
	const int32 SizeY = ReferenceTexture->GetDesc().Extent.Y;
	const EPixelFormat Format = ReferenceTexture->GetFormat();

	// Prepare description
	FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2D(TEXT("DisplayClusterFrameQueueCacheTexture"), SizeX, SizeY, Format)
		.SetClearValue(FClearValueBinding::Black)
		.SetNumMips(1)
		.SetFlags(ETextureCreateFlags::Dynamic)
		.AddFlags(ETextureCreateFlags::MultiGPUGraphIgnore)
		.SetInitialState(ERHIAccess::SRVMask);

	// Leave original flags, but make sure it's ResolveTargetable but not RenderTargetable
	ETextureCreateFlags Flags = ReferenceTexture->GetFlags();
	Flags &= ~ETextureCreateFlags::RenderTargetable;
	Flags |= ETextureCreateFlags::ResolveTargetable;
	Desc.SetFlags(Flags);

	// Create texture
	return RHICreateTexture(Desc);
}

void FDisplayClusterMediaInputBase::OnMediaEvent(EMediaEvent MediaEvent)
{
	switch (MediaEvent)
	{
	/** The player started connecting to the media source. */
	case EMediaEvent::MediaConnecting:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': Connection"), *GetMediaId());
		break;

	/** A new media source has been opened. */
	case EMediaEvent::MediaOpened:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': Opened"), *GetMediaId());
		break;

	/** The current media source has been closed. */
	case EMediaEvent::MediaClosed:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': Closed"), *GetMediaId());
		OnPlayerClosed();
		break;
		
	/** A media source failed to open. */
	case EMediaEvent::MediaOpenFailed:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': OpenFailed"), *GetMediaId());
		break;

	default:
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Media event for '%s': %d"), *GetMediaId(), static_cast<int32>(MediaEvent));
		break;
	}
}

bool FDisplayClusterMediaInputBase::StartPlayer()
{
	const bool bIsPlaying = MediaPlayer->OpenSource(MediaSource);
	if (bIsPlaying)
	{
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Started playing media: %s"), *GetMediaId());
	}
	else
	{
		UE_LOG(LogDisplayClusterMedia, Warning, TEXT("Couldn't start playing media: %s"), *GetMediaId());
	}

	return bIsPlaying;
}

void FDisplayClusterMediaInputBase::OnPlayerClosed()
{
	if (MediaPlayer && bWasPlayerStarted)
	{
		constexpr double Interval = 1.0;
		const double CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - LastRestartTimestamp > Interval)
		{
			UE_LOG(LogDisplayClusterMedia, Log, TEXT("MediaPlayer '%s' is in error, restarting it."), *GetMediaId());

			StartPlayer();
			LastRestartTimestamp = CurrentTime;
		}
	}
}
