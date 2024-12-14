// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/Renderers/MovieGraphDeferredPass.h"
#include "Graph/Nodes/MovieGraphDeferredPassNode.h"
#include "Graph/Nodes/MovieGraphCameraNode.h"
#include "Graph/MovieGraphDataTypes.h"
#include "Graph/MovieGraphDefaultRenderer.h"
#include "Graph/MovieGraphPipeline.h"
#include "Graph/MovieGraphRenderLayerSubsystem.h"
#include "Graph/MovieGraphBlueprintLibrary.h"
#include "MovieRenderOverlappedImage.h"
#include "MoviePipelineSurfaceReader.h"

#include "EngineModule.h"
#include "SceneManagement.h"
#include "CanvasTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LegacyScreenPercentageDriver.h"
#include "Materials/MaterialInterface.h"
#include "OpenColorIODisplayExtension.h"
#include "TextureResource.h"
#include "Tasks/Task.h"
#include "SceneView.h"

// For the 1D Weight table for accumulation
#include "MovieRenderPipelineDataTypes.h"
// For Sub-Pixel Jitter
#include "MoviePipelineUtils.h"

namespace UE::MovieGraph::Rendering
{

void FMovieGraphDeferredPass::Setup(TWeakObjectPtr<UMovieGraphDefaultRenderer> InRenderer, TWeakObjectPtr<UMovieGraphImagePassBaseNode> InRenderPassNode, const FMovieGraphRenderPassLayerData& InLayer)
{
	FMovieGraphImagePassBase::Setup(InRenderer, InRenderPassNode, InLayer);
	
	LayerData = InLayer;


	RenderDataIdentifier.RootBranchName = LayerData.BranchName;
	RenderDataIdentifier.LayerName = LayerData.LayerName;
	RenderDataIdentifier.RendererName = InRenderPassNode->GetRendererName();
	RenderDataIdentifier.SubResourceName = TEXT("beauty");
	
	RenderDataIdentifier.CameraName =  InLayer.CameraName;

	SceneViewState.Allocate(InRenderer->GetWorld()->GetFeatureLevel());

	// The InRenderPassNode is not initialized with user's config. Use InLayer to 
	// initialize the frames to delay for post submission.
	UMovieGraphRenderPassNode* RenderPassNode = InLayer.RenderPassNode.Get();
	FramesToDelayPostSubmission = RenderPassNode ? RenderPassNode->GetCoolingDownFrameCount() : 0;
	RemainingCooldownReadbackFrames = FramesToDelayPostSubmission;
}

void FMovieGraphDeferredPass::Teardown()
{
	FSceneViewStateInterface* SceneViewRef = SceneViewState.GetReference();
	if (SceneViewRef)
	{
		SceneViewRef->ClearMIDPool();
	}
	SceneViewState.Destroy();
}

void FMovieGraphDeferredPass::GatherOutputPasses(UMovieGraphEvaluatedConfig* InConfig, TArray<FMovieGraphRenderDataIdentifier>& OutExpectedPasses) const
{
	FMovieGraphImagePassBase::GatherOutputPasses(InConfig,OutExpectedPasses);
	
	// Add our pre-calculated identifier
	OutExpectedPasses.Add(RenderDataIdentifier);

	if (const UMovieGraphImagePassBaseNode* ParentNode = GetParentNode(InConfig))
	{
		for (const FMoviePipelinePostProcessPass& AdditionalPass : ParentNode->GetAdditionalPostProcessMaterials())
		{
			if (AdditionalPass.bEnabled)
			{
				if (const UMaterialInterface* Material = AdditionalPass.Material.LoadSynchronous())
				{
					FMovieGraphRenderDataIdentifier Identifier = RenderDataIdentifier;
					Identifier.SubResourceName = AdditionalPass.Name.IsEmpty() ? Material->GetName() : AdditionalPass.Name;
					OutExpectedPasses.Add(Identifier);
				}
			}
		}
	}
}

void FMovieGraphDeferredPass::AddReferencedObjects(FReferenceCollector& Collector)
{
	FSceneViewStateInterface* SceneViewRef = SceneViewState.GetReference();
	if (SceneViewRef)
	{
		SceneViewRef->AddReferencedObjects(Collector);
	}
}

FName FMovieGraphDeferredPass::GetBranchName() const
{
	return LayerData.BranchName;
}

bool FMovieGraphDeferredPass::ShouldDiscardOutput(const TSharedRef<FSceneViewFamilyContext>& InFamily, const UE::MovieGraph::DefaultRenderer::FCameraInfo& InCameraInfo) const
{
	TObjectPtr<UMovieGraphDefaultRenderer> GraphRenderer = GetRenderer().Get();
	if (!GraphRenderer)
	{
		return false;
	}

	if (UMovieGraphPipeline* Pipeline = GraphRenderer->GetOwningGraph())
	{
		// The deferred renderer should attempt to discard anything that isn't from the rendering state, as we don't need any data from
		// the warm-up or cool-down phases of the shot.
		return Pipeline->GetActiveShotList()[Pipeline->GetCurrentShotIndex()]->ShotInfo.State != EMovieRenderShotState::Rendering;
	}
	return false;
}


UMovieGraphImagePassBaseNode* FMovieGraphDeferredPass::GetParentNode(UMovieGraphEvaluatedConfig* InConfig) const
{
	// This is a bit of a workaround for the fact that the pass doesn't have a strong pointer to the node it's supposed to be associated with,
	// since that instance changes every frame. So instead we have a virtual function here so the node can look it up by type, and then we can
	// call a bunch of virtual functions on the right instance to fetch values.
	const bool bIncludeCDOs = true;
	UMovieGraphDeferredRenderPassNode* ParentNode = InConfig->GetSettingForBranch<UMovieGraphDeferredRenderPassNode>(GetBranchName(), bIncludeCDOs);
	if (!ensureMsgf(ParentNode, TEXT("DeferredPass should not exist without parent node in graph.")))
	{
		return nullptr;
	}

	return ParentNode;
}

void FMovieGraphDeferredPass::Render(const FMovieGraphTraversalContext& InFrameTraversalContext, const FMovieGraphTimeStepData& InTimeData)
{
	// ToDo: InFrameTraversalContext includes a copy of TimeData, but may be the one cached at the first temporal sample,
	// maybe we can combine, maybe we can't?
	TObjectPtr<UMovieGraphDefaultRenderer> GraphRenderer = GetRenderer().Get();
	if (!GraphRenderer)
	{
		return;
	}

	UMovieGraphImagePassBaseNode* ParentNodeThisFrame = GetParentNode(InTimeData.EvaluatedConfig);
	
	// We can only write rendered frames to disk right now (warm-up/cool-down indexes aren't propagated so files overwrite each other).
	const bool bWriteAllSamples = ParentNodeThisFrame->GetWriteAllSamples() && InFrameTraversalContext.Shot->ShotInfo.State == EMovieRenderShotState::Rendering;
	const bool bIsRenderingState = InFrameTraversalContext.Shot->ShotInfo.State == EMovieRenderShotState::Rendering ||
									InFrameTraversalContext.Shot->ShotInfo.State == EMovieRenderShotState::CoolingDown;
	int32 NumSpatialSamples = FMath::Max(1, bIsRenderingState ? ParentNodeThisFrame->GetNumSpatialSamples() : ParentNodeThisFrame->GetNumSpatialSamplesDuringWarmUp());

	const ESceneCaptureSource SceneCaptureSource = ParentNodeThisFrame->GetDisableToneCurve() ? ESceneCaptureSource::SCS_FinalColorHDR : ESceneCaptureSource::SCS_FinalToneCurveHDR;
	const EAntiAliasingMethod AntiAliasingMethod = ParentNodeThisFrame->GetAntiAliasingMethod();
	float OverscanFraction = 0.f;
	bool bOverrideCameraOverscan = false;
	const float TileOverlapPadRatio = 0.0f; // No tiling support right now

	// Camera nodes are optional
	const bool bIncludeCDOs = false;
	bool bRenderAllCameras = false;
	const UMovieGraphCameraSettingNode* CameraNode = InTimeData.EvaluatedConfig->GetSettingForBranch<UMovieGraphCameraSettingNode>(LayerData.BranchName, bIncludeCDOs);
	if (CameraNode)
	{
		OverscanFraction = FMath::Clamp(CameraNode->OverscanPercentage / 100.f, 0.f, 1.f);
		bRenderAllCameras = CameraNode->bRenderAllCameras;
		bOverrideCameraOverscan = CameraNode->bOverride_OverscanPercentage;
	}

	// ToDo: When tiling is used, this should be the size of the per-tile backbuffer
	const float CameraOverscan = GetRenderer()->GetCameraOverscan(LayerData.CameraIndex);
	FIntPoint AccumulatorResolution = UMovieGraphBlueprintLibrary::GetEffectiveOutputResolution(InTimeData.EvaluatedConfig, CameraOverscan);
	FIntPoint BackbufferResolution = AccumulatorResolution;
	
	DefaultRenderer::FRenderTargetInitParams RenderTargetInitParams = GetRenderTargetInitParams(InTimeData, AccumulatorResolution);
	UTextureRenderTarget2D* RenderTarget = GraphRenderer->GetOrCreateViewRenderTarget(RenderTargetInitParams, RenderDataIdentifier);
	FRenderTarget* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	check(RenderTargetResource);

	// Now we can actually construct our ViewFamily, SceneView, and submit it for Rendering + Readback
	for(int32 SpatialIndex = 0; SpatialIndex < NumSpatialSamples; SpatialIndex++)
	{
		// World should be paused for every spatial sample except the last one, so that
		// the view doesn't update histories until the end, allowing us to render the same
		// scene multiple times.
		const bool bWorldIsPaused = SpatialIndex < (NumSpatialSamples - 1);
		const int32 FrameIndex = InTimeData.RenderedFrameNumber * ((InTimeData.TemporalSampleCount * NumSpatialSamples) + (InTimeData.TemporalSampleIndex * NumSpatialSamples)) + SpatialIndex;

		// We only allow a spatial jitter if we have more than one sample
		FVector2f SpatialShiftAmount = FVector2f(0.f, 0.f);
		const bool bAntiAliasingAllowsJitter = AntiAliasingMethod == EAntiAliasingMethod::AAM_None;
		const bool bSampleCountsAllowsJitter = NumSpatialSamples > 1 || InTimeData.TemporalSampleCount > 1;
		if (bAntiAliasingAllowsJitter && bSampleCountsAllowsJitter)
		{
			const int32 NumSamplesPerOutputFrame = NumSpatialSamples * InTimeData.TemporalSampleCount;
			SpatialShiftAmount = UE::MoviePipeline::GetSubPixelJitter(FrameIndex, NumSamplesPerOutputFrame);
		}
		 
		// These are the parameters of our camera 
		UE::MovieGraph::DefaultRenderer::FCameraInfo CameraInfo = GetRenderer()->GetCameraInfo(LayerData.CameraIndex);

		CameraInfo.bAllowCameraAspectRatio = true;
		CameraInfo.TilingParams.TileSize = BackbufferResolution;
		CameraInfo.TilingParams.OverlapPad = FVector2f(0.f, 0.f); // No tile support
		CameraInfo.TilingParams.TileCount = FIntPoint(1, 1); // No tile support
		CameraInfo.TilingParams.TileIndexes = FIntPoint(0, 0); // No tile support
		CameraInfo.SamplingParams.TemporalSampleIndex = InTimeData.TemporalSampleIndex;
		CameraInfo.SamplingParams.TemporalSampleCount = InTimeData.TemporalSampleCount;
		CameraInfo.SamplingParams.SpatialSampleIndex = SpatialIndex;
		CameraInfo.SamplingParams.SpatialSampleCount = NumSpatialSamples;
		CameraInfo.ProjectionMatrixJitterAmount = FVector2D((SpatialShiftAmount.X) * 2.0f / (float)BackbufferResolution.X, SpatialShiftAmount.Y * -2.0f / (float)BackbufferResolution.Y);
		CameraInfo.bUseCameraManagerPostProcess = !bRenderAllCameras;
		
		if (bOverrideCameraOverscan)
		{
			CameraInfo.ViewInfo.ClearOverscan();
			CameraInfo.ViewInfo.ApplyOverscan(OverscanFraction);
		}
		else
		{
			// Current overscan is different from originally cached value, indicating an animated overscan value, so output a warning message
			if (CameraInfo.ViewInfo.GetOverscan() != CameraOverscan)
			{
				GetRenderer()->WarnAboutAnimatedOverscan(CameraOverscan);
			}
		}

		// ToDo: This math probably needs the per-tile, pre-overlapped size? 
		FIntPoint OverlappedPad = FIntPoint(FMath::CeilToInt(BackbufferResolution.X * TileOverlapPadRatio), FMath::CeilToInt(BackbufferResolution.Y * TileOverlapPadRatio));
		
		// For this particular tile, what is the offset into the output image
		FIntPoint OverlappedOffset = FIntPoint(CameraInfo.TilingParams.TileIndexes.X * BackbufferResolution.X - OverlappedPad.X, CameraInfo.TilingParams.TileIndexes.Y * BackbufferResolution.Y - OverlappedPad.Y);
		
		// Move the final render by this much in the accumulator to counteract the offset put into the view matrix.
		// Note that when bAllowSpatialJitter is false, SpatialShiftX/Y will always be zero.
		FVector2D OverlappedSubpixelShift = FVector2D(0.5f - SpatialShiftAmount.X, 0.5f - SpatialShiftAmount.Y);

		// The Scene View Family must be constructed first as the FSceneView needs it to be constructed
		UE::MovieGraph::Rendering::FViewFamilyInitData ViewFamilyInitData;
		ViewFamilyInitData.RenderTarget = RenderTargetResource;
		ViewFamilyInitData.World = GraphRenderer->GetWorld();
		ViewFamilyInitData.TimeData = InTimeData;
		ViewFamilyInitData.SceneCaptureSource = SceneCaptureSource;
		ViewFamilyInitData.bWorldIsPaused = bWorldIsPaused;
		ViewFamilyInitData.FrameIndex = FrameIndex;
		ViewFamilyInitData.AntiAliasingMethod = AntiAliasingMethod;
		ViewFamilyInitData.ShowFlags = ParentNodeThisFrame->GetShowFlags();
		ViewFamilyInitData.ViewModeIndex = ParentNodeThisFrame->GetViewModeIndex();
		ViewFamilyInitData.ProjectionMode = CameraInfo.ViewInfo.ProjectionMode;

		TSharedRef<FSceneViewFamilyContext> ViewFamily = CreateSceneViewFamily(ViewFamilyInitData);
		FSceneViewInitOptions SceneViewInitOptions = CreateViewInitOptions(CameraInfo, ViewFamily.ToSharedPtr().Get(), SceneViewState);
		
		
		CalculateProjectionMatrix(CameraInfo, SceneViewInitOptions, BackbufferResolution, AccumulatorResolution);
	
		// Modify the perspective matrix to do an off center projection, with overlap for high-res tiling
		const bool bOrthographic = CameraInfo.ViewInfo.ProjectionMode == ECameraProjectionMode::Type::Orthographic;
		ModifyProjectionMatrixForTiling(CameraInfo.TilingParams, bOrthographic, SceneViewInitOptions.ProjectionMatrix, CameraInfo.DoFSensorScale);
		
		// Scale the DoF sensor scale to counteract overscan, otherwise the size of Bokeh changes when you have Overscan enabled.
		// This needs to come after we modify it for Tiling.
		CameraInfo.DoFSensorScale *= 1.0 + CameraInfo.ViewInfo.GetOverscan();
		 
		// Construct a View to go within this family.
		FSceneView* NewView = CreateSceneView(SceneViewInitOptions, ViewFamily, CameraInfo);
		
		// Then apply Movie Render Queue specific overrides to the ViewFamily, and then to the SceneView.
		ApplyMovieGraphOverridesToViewFamily(ViewFamily, ViewFamilyInitData);
		
		// ToDo: This really only needs access to the ViewFamily for path tracer related things,
		// and would rather just take a FSceneView* 
		ApplyMovieGraphOverridesToSceneView(ViewFamily, ViewFamilyInitData, CameraInfo);
		
		FHitProxyConsumer* HitProxyConsumer = nullptr;
		const float DPIScale = 1.0f;
		FCanvas Canvas = FCanvas(RenderTargetResource, HitProxyConsumer, GraphRenderer->GetWorld(), GraphRenderer->GetWorld()->GetFeatureLevel(), FCanvas::CDM_DeferDrawing, DPIScale);
		
		// Construct the sample state that reflects the current render sample
		UE::MovieGraph::FMovieGraphSampleState SampleState;
		{
			// Take our per-frame Traversal Context and update it with context specific to this sample.
			FMovieGraphTraversalContext UpdatedTraversalContext = InFrameTraversalContext;
			UpdatedTraversalContext.Time = InTimeData;
			UpdatedTraversalContext.Time.SpatialSampleIndex = SpatialIndex;
			UpdatedTraversalContext.Time.SpatialSampleCount = NumSpatialSamples;
			UpdatedTraversalContext.RenderDataIdentifier = RenderDataIdentifier;

			SampleState.TraversalContext = MoveTemp(UpdatedTraversalContext);
			SampleState.BackbufferResolution = BackbufferResolution;
			SampleState.AccumulatorResolution = AccumulatorResolution;
			SampleState.bWriteSampleToDisk = bWriteAllSamples;
			SampleState.bRequiresAccumulator = InTimeData.bRequiresAccumulator || (NumSpatialSamples > 1);
			SampleState.bFetchFromAccumulator = InTimeData.bIsLastTemporalSampleForFrame && (SpatialIndex == (NumSpatialSamples - 1));
			SampleState.OverlappedPad = OverlappedPad;
			SampleState.OverlappedOffset = OverlappedOffset;
			SampleState.OverlappedSubpixelShift = OverlappedSubpixelShift;
			SampleState.OverscanFraction = CameraInfo.ViewInfo.GetOverscan();
			SampleState.bAllowOCIO = ParentNodeThisFrame->GetAllowOCIO();
			SampleState.bAllowsCompositing = ParentNodeThisFrame->GetAllowsCompositing();
			SampleState.SceneCaptureSource = SceneCaptureSource;
			SampleState.CompositingSortOrder = 10;
		}

		ApplyMovieGraphOverridesToSampleState(SampleState);

		if (UMovieGraphImagePassBaseNode* ParentNode = GetParentNode(InFrameTraversalContext.Time.EvaluatedConfig))
		{
			TSet<UMaterialInterface*> HighPrecisionMaterials;

			for (const FMoviePipelinePostProcessPass& PostProcessPass : ParentNode->GetAdditionalPostProcessMaterials())
			{
				UMaterialInterface* Material = PostProcessPass.Material.LoadSynchronous();
				
				if (!PostProcessPass.bEnabled || !Material)
				{
					continue;
				}
				
				auto BufferPipe = MakeShared<FImagePixelPipe, ESPMode::ThreadSafe>();
				
				NewView->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Add(Material);
				
				if (PostProcessPass.bHighPrecisionOutput)
				{
					HighPrecisionMaterials.Add(Material);
					BufferPipe->bIsExpecting32BitPixelData = true;
				}
				
				FMovieGraphRenderDataIdentifier Identifier = RenderDataIdentifier;
				Identifier.SubResourceName = PostProcessPass.Name.IsEmpty() ? Material->GetName() : PostProcessPass.Name;
				
				FMovieGraphSampleState PassSampleState = SampleState;
				PassSampleState.TraversalContext.RenderDataIdentifier = Identifier;
				
				// Give a lower priority to materials so they show up after the main pass in multi-layer exrs.
				PassSampleState.CompositingSortOrder = SampleState.CompositingSortOrder + 1;
				BufferPipe->AddEndpoint(MakeForwardingEndpoint(PassSampleState, InTimeData));

				NewView->FinalPostProcessSettings.BufferVisualizationPipes.Add(Material->GetFName(), BufferPipe);
			}
		}

		int32 NumValidMaterials = NewView->FinalPostProcessSettings.BufferVisualizationPipes.Num();
		NewView->FinalPostProcessSettings.bBufferVisualizationDumpRequired = NumValidMaterials > 0;
		NewView->FinalPostProcessSettings.bOverride_PathTracingEnableDenoiser = true;

		// The denoiser is disabled during warm-up frames.
		NewView->FinalPostProcessSettings.PathTracingEnableDenoiser = bIsRenderingState && ParentNodeThisFrame->GetAllowDenoiser();

		// Submit the renderer to be rendered
		GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.ToSharedPtr().Get());

		ENQUEUE_RENDER_COMMAND(TransitionTextureSRVState)(
			[RenderTargetResource](FRHICommandListImmediate& RHICmdList) mutable
			{
				// Transition our render target from a render target view to a shader resource view to allow the UMG preview material to read from this Render Target.
				RHICmdList.Transition(FRHITransitionInfo(RenderTargetResource->GetRenderTargetTexture(), ERHIAccess::RTV, ERHIAccess::SRVGraphicsPixel));
			});


		// If this was just to contribute to the history buffer, no need to go any further. Never discard if we're writing individual samples, though.
		bool bDiscardOutput = (InTimeData.bDiscardOutput || ShouldDiscardOutput(ViewFamily, CameraInfo)) && !SampleState.bWriteSampleToDisk;
		if (bDiscardOutput)
		{
			continue;
		}

		// Example Assumptions: 2 frame denoise temporal denoise with 8 temporal sub-samples. 
		// If you're using Cooldown Frames, we can get into a scenario where due to the particular render pass settings,
		// you don't need all the cooldown frames. If you're using Path Tracer's "Use Reference Motion Blur" then the 
		// above Discard is true for everything but the last sample. This means that we needed 8 Cool Down _samples_ to 
		// produce the two output frames (matching the 2 frame delay in the PT denoiser).
		// But if Use Reference Motion Blur is off, then the first two samples of the cool-down are enough to finish flushing
		// the PT denoiser, and the remaining 14 end up confusing the system because it gets data it doesn't think it should.
		if(InFrameTraversalContext.Shot->ShotInfo.State == EMovieRenderShotState::CoolingDown)
		{
			// When we're cooling down, we track the number of times we actually
			// go to do a readback (ie: pass the above bShouldDiscardOutput check)
			// and once we reach the number needed to actually flush the PT denoiser
			// we stop submitting.
			if (RemainingCooldownReadbackFrames == 0)
			{
				continue;
			}
			RemainingCooldownReadbackFrames--;
		}
		
		// Post-submission is a little bit complicated to allow supporting temporal denoisers in the Path Tracer.
		// When using the denoiser with a sample frame count of 2, for a frame to be produced it must look 2 frames
		// backwards, and 2 frames forwards, ie: to denoise Frame 5, we need to have rendered 3, 4, 5, 6, and 7.
		// The complication for this is that when we request a render and then immediately schedule a readback, when the
		// readback is completed the image will be the denoised result from a previous frame. ie: If on frame 5 you schedule
		// the readback, the result that will be copied to the CPU is the data from Frame 3.
		//
		// To resolve these issues, we capture the PostRendererSubmission and place it in a FIFO queue, and then when we schedule
		// a readback, we provide the old captured state, ie: on Frame 5 we provide Frame 3's data, and that will line up with the
		// image data actually generated on Frame 3 (which is what is returned by the GPU). A slight complication to this is that
		// PostRendererSubmission can no longer depend on any member state (since that would be using old data in combination with new),
		// but the one exception to this is that the current FCanvas is provided since it's only a wrapper for drawing letterboxing anyways.
		//
		// Under non-path-tracer temporal denoiser cases, this should effectively work out to be a no-op, the queue will dequeue immediately.

		FMovieGraphPostRendererSubmissionParams Params;
		Params.SampleState = SampleState;
		Params.RenderTargetInitParams = RenderTargetInitParams;
		Params.CameraInfo = CameraInfo;

		// Push the current frame into our FIFO queue
		SubmissionQueue.Enqueue(Params);

		// When we first start rendering we don't want to schedule a readback (as there isn't actually finished data to read back)
		// so we skip the first few frames. When we get to the end of a shot, we'll be in a cool-down period where we render extra
		// frames to allow finishing the denoising on the previous "real" frames. Those frames can't have discard output set on them,
		// otherwise we won't actually read back the end of the "real" frames. This means the queue will be left with some extra data
		// in it (for the cool-down frames which were calculated and submitted but never themselves get read back) but that's okay.
		if (FramesToDelayPostSubmission == 0)
		{
			// Now we schedule a readback using the oldest data.
			FMovieGraphPostRendererSubmissionParams PostParamsToUse;
			if (SubmissionQueue.Dequeue(PostParamsToUse))
			{
				// It's okay that we use the current FCanvas here as it's just a vessel to draw letterboxing based on state captured by the params.
				PostRendererSubmission(PostParamsToUse.SampleState, PostParamsToUse.RenderTargetInitParams, Canvas, PostParamsToUse.CameraInfo);
			}
			else
			{
				UE_LOG(LogMovieRenderPipeline, Error, TEXT("De-queue post-submission parameters failed. Attempted to send a frame to post-render submission, but no frames were available in the FIFO queue."));
			}
		}
		else
		{
			FramesToDelayPostSubmission--;
		}
	}
}

void FMovieGraphDeferredPass::PostRendererSubmission(
	const UE::MovieGraph::FMovieGraphSampleState& InSampleState,
	const UE::MovieGraph::DefaultRenderer::FRenderTargetInitParams& InRenderTargetInitParams, FCanvas& InCanvas, const UE::MovieGraph::DefaultRenderer::FCameraInfo& InCameraInfo) const
{
	TObjectPtr<UMovieGraphDefaultRenderer> GraphRenderer = GetRenderer().Get();
	if (!GraphRenderer)
	{
		return;
	}

	// Draw letterboxing
	// ToDo: Multi-camera support
	if(InCameraInfo.ViewInfo.bConstrainAspectRatio)
	{
		const FMinimalViewInfo CameraCache = InCameraInfo.ViewInfo;
		
		// Taking overscan into account.
		const FIntPoint FullOutputSize = InSampleState.AccumulatorResolution;
	
		const float OutputSizeAspectRatio = FullOutputSize.X / (float)FullOutputSize.Y;
		const float CameraAspectRatio = InCameraInfo.bAllowCameraAspectRatio ? CameraCache.AspectRatio : OutputSizeAspectRatio;
	
		const FIntPoint ConstrainedFullSize = CameraAspectRatio > OutputSizeAspectRatio ?
			FIntPoint(FullOutputSize.X, FMath::CeilToInt((double)FullOutputSize.X / (double)CameraAspectRatio)) :
			FIntPoint(FMath::CeilToInt(CameraAspectRatio * FullOutputSize.Y), FullOutputSize.Y);
	
		const FIntPoint TileViewMin = InSampleState.OverlappedOffset;
		const FIntPoint TileViewMax = TileViewMin + InSampleState.BackbufferResolution;
	
		// Camera ratio constrained rect, clipped by the tile rect
		FIntPoint ConstrainedViewMin = (FullOutputSize - ConstrainedFullSize) / 2;
		FIntPoint ConstrainedViewMax = ConstrainedViewMin + ConstrainedFullSize;
		ConstrainedViewMin = FIntPoint(FMath::Clamp(ConstrainedViewMin.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMin.Y, TileViewMin.Y, TileViewMax.Y));
		ConstrainedViewMax = FIntPoint(FMath::Clamp(ConstrainedViewMax.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMax.Y, TileViewMin.Y, TileViewMax.Y));
	
		// Difference between the clipped constrained rect and the tile rect
		const FIntPoint OffsetMin = ConstrainedViewMin - TileViewMin;
		const FIntPoint OffsetMax = TileViewMax - ConstrainedViewMax;
	
		// Clear left
		if (OffsetMin.X > 0)
		{
			InCanvas.DrawTile(0, 0, OffsetMin.X, InSampleState.BackbufferResolution.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear right
		if (OffsetMax.X > 0)
		{
			InCanvas.DrawTile(InSampleState.BackbufferResolution.X - OffsetMax.X, 0, InSampleState.BackbufferResolution.X, InSampleState.BackbufferResolution.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear top
		if (OffsetMin.Y > 0)
		{
			InCanvas.DrawTile(0, 0, InSampleState.BackbufferResolution.X, OffsetMin.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear bottom
		if (OffsetMax.Y > 0)
		{
			InCanvas.DrawTile(0, InSampleState.BackbufferResolution.Y - OffsetMax.Y, InSampleState.BackbufferResolution.X, InSampleState.BackbufferResolution.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
	
		InCanvas.Flush_GameThread(true);
	}
	
	FMovieGraphImagePassBase::PostRendererSubmission(InSampleState, InRenderTargetInitParams, InCanvas, InCameraInfo);
}
} // namespace UE::MovieGraph::Rendering
