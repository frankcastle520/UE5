// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Capture/DisplayClusterMediaCaptureBase.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"

class FRDGBuilder;
class FSceneView;
class FSceneViewFamily;
class IDisplayClusterViewport;
class IDisplayClusterViewportProxy;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;


/**
 * Viewport media capture
 */
class FDisplayClusterMediaCaptureViewport
	: public FDisplayClusterMediaCaptureBase
{
public:
	FDisplayClusterMediaCaptureViewport(
		const FString& MediaId,
		const FString& ClusterNodeId,
		const FString& ViewportId,
		UMediaOutput* MediaOutput,
		UDisplayClusterMediaOutputSynchronizationPolicy* SyncPolicy = nullptr,
		bool bInLateOCIO = false
	);

public:
	/** Start capturing */
	virtual bool StartCapture() override;

	/** Stop capturing */
	virtual void StopCapture() override;

	/** Returns viewport ID that is configured for capture */
	const FString& GetViewportId() const
	{
		return ViewportId;
	}

	/** Returns texture size of a viewport assigned to capture (main thread) */
	virtual FIntPoint GetCaptureSize() const override;

	/** Provides default texture size from config */
	virtual bool GetCaptureSizeFromConfig(FIntPoint& OutSize) const;

	/** Provides texture size from a game proxy (if available) */
	bool GetCaptureSizeFromGameProxy(FIntPoint& OutSize) const;

private:

	/** UpdateViewportMediaState callback to configure media state for a viewoprt */
	void OnUpdateViewportMediaState(IDisplayClusterViewport* InViewport, EDisplayClusterViewportMediaState& InOutMediaState);

	/** PostRenderViewFamily callback handler where data is captured (no late OCIO) */
	void OnPostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamily, const IDisplayClusterViewportProxy* ViewportProxy);

	/** PostTonemapPass callback handler (late OCIO) */
	void OnPostTonemapPass_RenderThread(FRDGBuilder& GraphBuilder, const IDisplayClusterViewportProxy* ViewportProxy, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, const uint32 ContextNum);

private:

	/** Returns size of the viewport bound to this media */
	virtual FIntPoint GetViewportSize() const;

private:
	/** Viewport ID assigned to capture */
	const FString ViewportId;
};
