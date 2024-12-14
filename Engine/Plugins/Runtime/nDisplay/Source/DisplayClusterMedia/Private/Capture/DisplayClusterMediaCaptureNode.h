// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Capture/DisplayClusterMediaCaptureBase.h"

#include "RHIResources.h"

class FRHICommandListImmediate;
class FViewport;
class IDisplayClusterViewportManagerProxy;


/**
 * Node backbuffer media capture
 */
class FDisplayClusterMediaCaptureNode
	: public FDisplayClusterMediaCaptureBase
{
public:
	FDisplayClusterMediaCaptureNode(
		const FString& MediaId,
		const FString& ClusterNodeId,
		UMediaOutput* MediaOutput,
		UDisplayClusterMediaOutputSynchronizationPolicy* SyncPolicy = nullptr
	);

public:
	/** Start backbuffer capture */
	virtual bool StartCapture() override;

	/** Stop backbuffer capture */
	virtual void StopCapture() override;

protected:
	/** Returns backbuffer size */
	virtual FIntPoint GetCaptureSize() const override;

private:
	/** PostBackbufferUpdated event handler */
	void OnPostBackbufferUpdated_RenderThread(FRHICommandListImmediate& RHICmdList, FViewport* Viewport);
};
