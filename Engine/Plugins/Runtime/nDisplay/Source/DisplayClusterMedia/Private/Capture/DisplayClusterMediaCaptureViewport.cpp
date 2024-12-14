// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/DisplayClusterMediaCaptureViewport.h"

#include "IDisplayCluster.h"
#include "IDisplayClusterCallbacks.h"

#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "DisplayClusterMediaCVars.h"
#include "DisplayClusterMediaLog.h"
#include "DisplayClusterRootActor.h"

#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "PostProcess/PostProcessMaterialInputs.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Context.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettings.h"

#include "RHICommandList.h"
#include "RHIResources.h"


FDisplayClusterMediaCaptureViewport::FDisplayClusterMediaCaptureViewport(
	const FString& InMediaId,
	const FString& InClusterNodeId,
	const FString& InViewportId,
	UMediaOutput* InMediaOutput,
	UDisplayClusterMediaOutputSynchronizationPolicy* SyncPolicy,
	bool bInLateOCIO
)
	: FDisplayClusterMediaCaptureBase(InMediaId, InClusterNodeId, InMediaOutput, SyncPolicy, bInLateOCIO)
	, ViewportId(InViewportId)
{
}


bool FDisplayClusterMediaCaptureViewport::StartCapture()
{
	// Subscribe for events
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterUpdateViewportMediaState().AddRaw(this, &FDisplayClusterMediaCaptureViewport::OnUpdateViewportMediaState);

	// Depending on late OCIO configuration, grab the image in different places
	if (IsLateOCIO())
	{
		IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostTonemapPass_RenderThread().AddRaw(this, &FDisplayClusterMediaCaptureViewport::OnPostTonemapPass_RenderThread);
	}
	else
	{
		IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostRenderViewFamily_RenderThread().AddRaw(this, &FDisplayClusterMediaCaptureViewport::OnPostRenderViewFamily_RenderThread);
	}

	// Start capture
	const bool bStarted = FDisplayClusterMediaCaptureBase::StartCapture();

	return bStarted;
}

void FDisplayClusterMediaCaptureViewport::StopCapture()
{
	// Unsubscribe from external events/callbacks
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostRenderViewFamily_RenderThread().RemoveAll(this);
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterPostTonemapPass_RenderThread().RemoveAll(this);
	IDisplayCluster::Get().GetCallbacks().OnDisplayClusterUpdateViewportMediaState().RemoveAll(this);

	// Stop capturing
	FDisplayClusterMediaCaptureBase::StopCapture();
}

void FDisplayClusterMediaCaptureViewport::OnUpdateViewportMediaState(IDisplayClusterViewport* InViewport, EDisplayClusterViewportMediaState& InOutMediaState)
{
	// Set capture flag for the matching viewport
	if (InViewport && InViewport->GetId().Equals(GetViewportId(), ESearchCase::IgnoreCase))
	{
		// Raise flags that this viewport will be captured by media.
		InOutMediaState |= EDisplayClusterViewportMediaState::Capture;

		// Late OCIO flag
		if (IsLateOCIO())
		{
			InOutMediaState |= EDisplayClusterViewportMediaState::CaptureLateOCIO;
		}
	}
}

FIntPoint FDisplayClusterMediaCaptureViewport::GetCaptureSize() const
{
	return GetViewportSize();
}

FIntPoint FDisplayClusterMediaCaptureViewport::GetViewportSize() const
{
	FIntPoint CaptureSize{ FIntPoint::ZeroValue };

	if (GetCaptureSizeFromGameProxy(CaptureSize))
	{
		UE_LOG(LogDisplayClusterMedia, Verbose, TEXT("'%s' acquired capture size from game proxy [%d, %d]"), *GetMediaId(), CaptureSize.X, CaptureSize.Y);
	}
	else if (GetCaptureSizeFromConfig(CaptureSize))
	{
		UE_LOG(LogDisplayClusterMedia, Verbose, TEXT("'%s' acquired capture size from config [%d, %d]"), *GetMediaId(), CaptureSize.X, CaptureSize.Y);
	}
	else
	{
		UE_LOG(LogDisplayClusterMedia, Verbose, TEXT("'%s' couldn't acquire capture"), *GetMediaId());
	}

	return CaptureSize;
}

bool FDisplayClusterMediaCaptureViewport::GetCaptureSizeFromConfig(FIntPoint& OutSize) const
{
	if (const ADisplayClusterRootActor* const ActiveRootActor = IDisplayCluster::Get().GetGameMgr()->GetRootActor())
	{
		if (const UDisplayClusterConfigurationData* const ConfigData = ActiveRootActor->GetConfigData())
		{
			const FString& NodeId = GetClusterNodeId();
			if (const UDisplayClusterConfigurationViewport* const ViewportCfg = ConfigData->GetViewport(NodeId, ViewportId))
			{
				const FIntRect ViewportRect = ViewportCfg->Region.ToRect();
				OutSize = FIntPoint(ViewportRect.Width(), ViewportRect.Height());

				return true;
			}
		}
	}

	return false;
}

bool FDisplayClusterMediaCaptureViewport::GetCaptureSizeFromGameProxy(FIntPoint& OutSize) const
{
	// We need to get actual texture size for the viewport
	if (const IDisplayClusterRenderManager* const RenderMgr = IDisplayCluster::Get().GetRenderMgr())
	{
		if (const IDisplayClusterViewportManager* const ViewportMgr = RenderMgr->GetViewportManager())
		{
			if (const IDisplayClusterViewport* const Viewport = ViewportMgr->FindViewport(ViewportId))
			{
				const TArray<FDisplayClusterViewport_Context>& Contexts = Viewport->GetContexts();
				if (Contexts.Num() > 0)
				{
					OutSize = Contexts[0].RenderTargetRect.Size();
					return true;
				}
			}
		}
	}

	return false;
}

void FDisplayClusterMediaCaptureViewport::OnPostTonemapPass_RenderThread(FRDGBuilder& GraphBuilder, const IDisplayClusterViewportProxy* ViewportProxy, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const uint32 ContextNum)
{
	checkSlow(ViewportProxy);

	if (!IsLateOCIO())
	{
		return;
	}

	// Media subsystem does not support stereo, therefore we process context 0 only
	if (ContextNum != 0)
	{
		return;
	}

	// Check if proxy object is valid
	if (!ViewportProxy)
	{
		return;
	}

	// Make sure this is our viewport
	const bool bMatchingViewport = ViewportProxy->GetId().Equals(GetViewportId(), ESearchCase::IgnoreCase);
	if (!bMatchingViewport)
	{
		return;
	}

	// Get current SceneColor texture
	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	// Pass it to the media capture pipeline
	if (SceneColor.IsValid())
	{
		FMediaOutputTextureInfo TextureInfo{ SceneColor.Texture, SceneColor.ViewRect };
		ExportMediaData_RenderThread(GraphBuilder, TextureInfo);
	}
}

void FDisplayClusterMediaCaptureViewport::OnPostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamily, const IDisplayClusterViewportProxy* ViewportProxy)
{
	checkSlow(ViewportProxy);

	// Nothing to do if late OCIO is required. The texture has been exported already on PostTonemap callback.
	if (IsLateOCIO())
	{
		return;
	}

	// Otherwise, find our viewport and export its texture
	if (ViewportProxy && ViewportProxy->GetId().Equals(GetViewportId(), ESearchCase::IgnoreCase))
	{
		TArray<FRHITexture*> Textures;
		TArray<FIntRect>     Regions;

		// Get RHI texture and pass it to the media capture pipeline
		if (ViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InternalRenderTargetResource, Textures, Regions))
		{
			if (Textures.Num() > 0 && Regions.Num() > 0 && Textures[0])
			{
				FRDGTextureRef SrcTextureRef = RegisterExternalTexture(GraphBuilder, Textures[0], TEXT("DCMediaOutViewportTex"));

				FMediaOutputTextureInfo TextureInfo{ SrcTextureRef, Regions[0] };
				ExportMediaData_RenderThread(GraphBuilder, TextureInfo);
			}
		}
	}
}
