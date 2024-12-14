// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneViewExtension.h"

#include "LensFileRendering.h"

/**
 * View extension drawing distortion/undistortion displacement maps
 */
class FLensDistortionSceneViewExtension : public FSceneViewExtensionBase
{
public:

	FLensDistortionSceneViewExtension(const FAutoRegister& AutoRegister);

	//~ Begin ISceneViewExtension interface	
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};

	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	//~End ISceneVIewExtension interface

public:
	/** Update the distortion state and blending params for the input camera */
	void UpdateDistortionState_AnyThread(ACameraActor* CameraActor, FDisplacementMapBlendingParams DistortionState);

	/** Remove the distortion state and blending params for the input camera */
	void ClearDistortionState_AnyThread(ACameraActor* CameraActor);

private:
	/** Use the input distortion state to draw a distortion displacement map */
	void DrawDisplacementMap_RenderThread(FRDGBuilder& GraphBuilder, const FLensDistortionState& CurrentState, float InverseOverscan, float CameraOverscan, FRDGTextureRef& OutDistortionMapWithOverscan);

	/** Use the input blend parameters to draw multiple displacement maps and blend them together into a final distortion displacement map */
	void BlendDisplacementMaps_RenderThread(FRDGBuilder& GraphBuilder, const FDisplacementMapBlendingParams& BlendState, float InverseOverscan, float CameraOverscan, FRDGTextureRef& OutDistortionMapWithOverscan);

	/** Crop the input overscanned distortion map to the original requested resolution */
	void CropDisplacementMap_RenderThread(FRDGBuilder& GraphBuilder, const FRDGTextureRef& InDistortionMapWithOverscan, FRDGTextureRef& OutDistortionMap);

	/** Invert the input distortion map to generate a matching undistortion map (with no overscan) */
	void InvertDistortionMap_RenderThread(FRDGBuilder& GraphBuilder, const FRDGTextureRef& InDistortionMap, FRDGTextureRef& OutUndistortionMap);

private:
	/** Map of cameras to their associated distortion state and blending parameters, used to determine if and how displacement maps should be rendered for a specific view */
	TMap<TWeakObjectPtr<ACameraActor>, FDisplacementMapBlendingParams> DistortionStateMap;

	/** Critical section to lock access to the distortion state map when potentially being accessed from multiple threads */
	mutable FCriticalSection DistortionStateMapCriticalSection;
};
