// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneViewExtension.h"
#include "RenderGraphFwd.h"
#include "RendererInterface.h"
#include "Containers/ContainersFwd.h"

class FViewInfo;
struct FScreenPassRenderTarget;
struct FScreenPassTexture;

class FHoldoutCompositeSceneViewExtension : public FWorldSceneViewExtension
{
public:
	FHoldoutCompositeSceneViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld);
	~FHoldoutCompositeSceneViewExtension();

	/* Register primitives for compositing. */
	void RegisterPrimitives(TArrayView<TSoftObjectPtr<UPrimitiveComponent>> InPrimitiveComponents, bool bInHoldoutState=true);

	/* Unregister primitives for compositing. */
	void UnregisterPrimitives(TArrayView<TSoftObjectPtr<UPrimitiveComponent>> InPrimitiveComponents, bool bInHoldoutState=false);

	/* Called by the custom render pass to store its view render target for this frame. */
	template<typename T>
	void CollectCustomRenderTarget(uint32 InViewId, T InRenderTarget)
	{
		CustomRenderTargetPerView_RenderThread.Add(InViewId, InRenderTarget);
	}

	//~ Begin ISceneViewExtension Interface
	virtual int32 GetPriority() const override;
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& InView, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	//~ End ISceneViewExtension Interface

protected:
	//~ Begin ISceneViewExtension Interface
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End ISceneViewExtension Interface

private:

	FRDGTextureRef GetCustomRenderPassTexture(FRDGBuilder& GraphBuilder, const FSceneView& InView) const;
	class FHoldoutCompositeCommonParameters BuildCommonCompositeParameters(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FScreenPassTexture& SceneColor, const FScreenPassRenderTarget& Output, bool bIsSceneColorUndistorted = false);

	FScreenPassTexture PostProcessPassSSRInput_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);
	FScreenPassTexture PostProcessPassAfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessMaterialInputs& Inputs);

	// Collection of primitives to render as a custom render pass and composite after post-processing.
	TSet<TSoftObjectPtr<UPrimitiveComponent>> CompositePrimitives;

	// Custom render pass render targets for each active view
	TMap<uint32, TRefCountPtr<IPooledRenderTarget>> CustomRenderTargetPerView_RenderThread;

	// Flag to enable global exposure on the composited render
	std::atomic_bool bCompositeFollowsSceneExposure = false;

	// Flag to enable composite into screen-space reflections
	std::atomic_bool bCompositeSupportsSSR = false;
};

