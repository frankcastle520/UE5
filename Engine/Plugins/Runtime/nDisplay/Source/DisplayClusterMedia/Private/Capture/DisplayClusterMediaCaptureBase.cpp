// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/DisplayClusterMediaCaptureBase.h"
#include "DisplayClusterMediaHelpers.h"
#include "DisplayClusterMediaLog.h"
#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"

#include "DisplayClusterConfigurationTypes_Media.h"
#include "DisplayClusterConfigurationTypes_MediaSync.h"

#include "MediaCapture.h"
#include "MediaOutput.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHICommandList.h"
#include "RHIUtilities.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"


FDisplayClusterMediaCaptureBase::FDisplayClusterMediaCaptureBase(
	const FString& InMediaId,
	const FString& InClusterNodeId,
	UMediaOutput* InMediaOutput,
	UDisplayClusterMediaOutputSynchronizationPolicy* InSyncPolicy,
	bool bInLateOCIO
)
	: FDisplayClusterMediaBase(InMediaId, InClusterNodeId, bInLateOCIO)
	, SyncPolicy(InSyncPolicy)
{
	checkSlow(InMediaOutput);
	MediaOutput = DuplicateObject(InMediaOutput, GetTransientPackage());
	checkSlow(MediaOutput);

	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostTick().AddRaw(this, &FDisplayClusterMediaCaptureBase::OnPostClusterTick);
}


FDisplayClusterMediaCaptureBase::~FDisplayClusterMediaCaptureBase()
{
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostTick().RemoveAll(this);
}

void FDisplayClusterMediaCaptureBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (MediaOutput)
	{
		Collector.AddReferencedObject(MediaOutput);
	}

	if (MediaCapture)
	{
		Collector.AddReferencedObject(MediaCapture);
	}

	if (SyncPolicy)
	{
		Collector.AddReferencedObject(SyncPolicy);
	}
}

bool FDisplayClusterMediaCaptureBase::StartCapture()
{
	if (MediaOutput && !MediaCapture)
	{
		MediaCapture = MediaOutput->CreateMediaCapture();
		if (IsValid(MediaCapture))
		{
			MediaCapture->SetMediaOutput(MediaOutput);

			// Initialize and start capture synchronization
			if (IsValid(SyncPolicy))
			{
				SyncPolicyHandler = SyncPolicy->GetHandler();
				if (SyncPolicyHandler)
				{
					if (SyncPolicyHandler->IsCaptureTypeSupported(MediaCapture))
					{
						if (SyncPolicyHandler->StartSynchronization(MediaCapture, GetMediaId()))
						{
							UE_LOG(LogDisplayClusterMedia, Log, TEXT("MediaCapture '%s' started synchronization type '%s'."), *GetMediaId(), *SyncPolicy->GetName());
						}
						else
						{
							UE_LOG(LogDisplayClusterMedia, Warning, TEXT("MediaCapture '%s': couldn't start synchronization."), *GetMediaId());
						}
					}
					else
					{
						UE_LOG(LogDisplayClusterMedia, Warning, TEXT("MediaCapture '%s' is not compatible with media SyncPolicy '%s'."), *GetMediaId(), *SyncPolicy->GetName());
					}
				}
				else
				{
					UE_LOG(LogDisplayClusterMedia, Warning, TEXT("Could not create media sync policy handler from '%s'."), *SyncPolicy->GetName());
				}
			}

			bWasCaptureStarted = StartMediaCapture();
			return bWasCaptureStarted;
		}
	}

	return false;
}

void FDisplayClusterMediaCaptureBase::StopCapture()
{
	// Stop synchronization
	if (SyncPolicyHandler)
	{
		SyncPolicyHandler->StopSynchronization();
	}

	// Stop capture
	if (MediaCapture)
	{
		MediaCapture->StopCapture(false);
		MediaCapture = nullptr;
		bWasCaptureStarted = false;
	}
}

void FDisplayClusterMediaCaptureBase::ExportMediaData_RenderThread(FRDGBuilder& GraphBuilder, const FMediaOutputTextureInfo& TextureInfo)
{
	// Check if source texture is valid
	if (!TextureInfo.Texture)
	{
		UE_LOG(LogDisplayClusterMedia, Warning, TEXT("MediaCapture '%s': invalid source texture on RT frame %lu"), *GetMediaId(), GFrameCounterRenderThread);
		return;
	}

	MediaCapture->SetValidSourceGPUMask(GraphBuilder.RHICmdList.GetGPUMask());

	const FIntPoint SrcTextureSize = TextureInfo.Texture->Desc.Extent;
	const FIntPoint SrcRegionSize  = TextureInfo.Region.Size();

	LastSrcRegionSize = FIntSize(SrcRegionSize);

	UE_LOG(LogDisplayClusterMedia, VeryVerbose, TEXT("MediaCapture '%s': exporting texture [size=%dx%d, rect=%dx%d] on RT frame '%lu'..."),
		*GetMediaId(), SrcTextureSize.X, SrcTextureSize.Y, SrcRegionSize.X, SrcRegionSize.Y, GFrameCounterRenderThread);

	// Capture
	bool bCaptureSucceeded = MediaCapture->TryCaptureImmediate_RenderThread(GraphBuilder, TextureInfo.Texture, TextureInfo.Region);
	if(!bCaptureSucceeded)
	{
		UE_LOG(LogDisplayClusterMedia, VeryVerbose, TEXT("MediaCapture '%s': failed to capture resource"), *GetMediaId());
	}
}

void FDisplayClusterMediaCaptureBase::OnPostClusterTick()
{
	if (MediaCapture)
	{
		EMediaCaptureState MediaCaptureState = MediaCapture->GetState();

		// If we're capturing but the desired capture resolution does not match the texture being captured,
		// restart the capture with the updated size.

		if (MediaCaptureState == EMediaCaptureState::Capturing)
		{
			const FIntPoint LastSrcRegionIntPoint = LastSrcRegionSize.load().ToIntPoint();
			const FIntPoint DesiredSize = MediaCapture->GetDesiredSize();

			if (DesiredSize != LastSrcRegionIntPoint)
			{
				UE_LOG(LogDisplayClusterMedia, Log, TEXT("Stopping MediaCapture '%s' because its DesiredSize (%d, %d) doesn't match the captured texture size (%d, %d)"), 
					*GetMediaId(), DesiredSize.X, DesiredSize.Y, LastSrcRegionIntPoint.X, LastSrcRegionIntPoint.Y);

				MediaCapture->StopCapture(false /* bAllowPendingFrameToBeProcess */);
				MediaCaptureState = MediaCapture->GetState(); // Re-sample state to restart the media capture right away
			}
		}

		const bool bMediaCaptureNeedsRestart = (MediaCaptureState == EMediaCaptureState::Error) || (MediaCaptureState == EMediaCaptureState::Stopped);

		if (!bWasCaptureStarted || bMediaCaptureNeedsRestart)
		{
			constexpr double Interval = 1.0;
			const double CurrentTime = FPlatformTime::Seconds();

			if (CurrentTime - LastRestartTimestamp > Interval)
			{
				UE_LOG(LogDisplayClusterMedia, Log, TEXT("MediaCapture '%s' is in error or stopped, restarting it."), *GetMediaId());

				bWasCaptureStarted = StartMediaCapture();
				LastRestartTimestamp = CurrentTime;
			}
		}
	}
}

bool FDisplayClusterMediaCaptureBase::StartMediaCapture()
{
	FRHICaptureResourceDescription Descriptor;

	if (LastSrcRegionSize.load().ToIntPoint() == FIntPoint::ZeroValue)
	{
		Descriptor.ResourceSize = GetCaptureSize();
	}
	else
	{
		Descriptor.ResourceSize = LastSrcRegionSize.load().ToIntPoint();
	}
	
	if (Descriptor.ResourceSize == FIntPoint::ZeroValue)
	{
		return false;
	}

	FMediaCaptureOptions MediaCaptureOptions;
	MediaCaptureOptions.NumberOfFramesToCapture = -1;
	MediaCaptureOptions.bAutoRestartOnSourceSizeChange = false; // true won't work due to MediaCapture auto-changing crop mode to custom when capture region is specified.
	MediaCaptureOptions.bSkipFrameWhenRunningExpensiveTasks = false;
	MediaCaptureOptions.OverrunAction = EMediaCaptureOverrunAction::Flush;

	const bool bCaptureStarted = MediaCapture->CaptureRHITexture(Descriptor, MediaCaptureOptions);
	if (bCaptureStarted)
	{
		UE_LOG(LogDisplayClusterMedia, Log, TEXT("Started media capture: '%s'"), *GetMediaId());
	}
	else
	{
		UE_LOG(LogDisplayClusterMedia, Warning, TEXT("Couldn't start media capture '%s'"), *GetMediaId());
	}

	return bCaptureStarted;
}
