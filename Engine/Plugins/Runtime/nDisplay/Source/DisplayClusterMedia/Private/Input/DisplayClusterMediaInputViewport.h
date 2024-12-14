// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/DisplayClusterMediaInputBase.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"

#include "OpenColorIORendering.h"

class FRHICommandListImmediate;
class FViewport;
class FSceneViewFamilyContext;
class IDisplayClusterViewport;
class IDisplayClusterViewportManagerProxy;


/**
 * Viewport media input adapter
 */
class FDisplayClusterMediaInputViewport
	: public FDisplayClusterMediaInputBase
{
public:
	FDisplayClusterMediaInputViewport(
		const FString& MediaId,
		const FString& ClusterNodeId,
		const FString& ViewportId,
		UMediaSource* MediaSource,
		bool bInLateOCIO = false
	);

public:
	/** Start playback */
	virtual bool Play() override;

	/** Stop playback */
	virtual void Stop() override;

	/** Returns viewport ID bound for playback */
	const FString& GetViewportId() const
	{
		return ViewportId;
	}

private:

	/** PreSubmitViewFamilies event handler. It's used to initialize media on start. */
	void OnPreSubmitViewFamilies(TArray<FSceneViewFamilyContext*>&);

	/** PostCrossGpuTransfer callback handler where media data is pushed into nDisplay internal buffers */
	void OnPostCrossGpuTransfer_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* ViewportManagerProxy, FViewport* Viewport);

	/** UpdateViewportMediaState callback to configure media state for a viewoprt */
	void OnUpdateViewportMediaState(IDisplayClusterViewport* InViewport, EDisplayClusterViewportMediaState& InOutMediaState);

private:

	/** Viewport ID assigned for this media input */
	const FString ViewportId;

	/** OCIO conversion pass resources (render thread data) */
	FOpenColorIORenderPassResources OCIOPassResources_RT;
};
