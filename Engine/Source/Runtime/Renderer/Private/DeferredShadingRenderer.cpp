// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.cpp: Top level rendering loop for deferred shading
=============================================================================*/

#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "VelocityRendering.h"
#include "SingleLayerWaterRendering.h"
#include "SkyAtmosphereRendering.h"
#include "VolumetricCloudRendering.h"
#include "SparseVolumeTexture/SparseVolumeTextureViewerRendering.h"
#include "VolumetricRenderTarget.h"
#include "ScenePrivate.h"
#include "SceneOcclusion.h"
#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/PostProcessVisualizeCalibrationMaterial.h"
#include "PostProcess/TemporalAA.h"
#include "CompositionLighting/CompositionLighting.h"
#include "FXSystem.h"
#include "OneColorShader.h"
#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "GlobalDistanceField.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessEyeAdaptation.h"
#include "DistanceFieldAtlas.h"
#include "EngineModule.h"
#include "SceneViewExtension.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "RendererModule.h"
#include "VT/VirtualTextureFeedback.h"
#include "VT/VirtualTextureSystem.h"
#include "GPUScene.h"
#include "PathTracing.h"
#include "RayTracing/RayTracing.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "RayTracing/RayTracingLighting.h"
#include "RayTracing/RayTracingDecals.h"
#include "RayTracing/RayTracingScene.h"
#include "RayTracing/RayTracingInstanceMask.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingSkinnedGeometry.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "ShaderPrint.h"
#include "GPUSortManager.h"
#include "HairStrands/HairStrandsRendering.h"
#include "HairStrands/HairStrandsData.h"
#include "PhysicsField/PhysicsFieldComponent.h"
#include "PhysicsFieldRendering.h"
#include "NaniteVisualizationData.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "Rendering/NaniteCoarseMeshStreamingManager.h"
#include "SceneTextureReductions.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"
#include "Substrate/Substrate.h"
#include "Lumen/Lumen.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "Rendering/RayTracingGeometryManager.h"
#include "InstanceCulling/InstanceCullingManager.h"
#include "InstanceCulling/InstanceCullingOcclusionQuery.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/SubsurfaceProfile.h"
#include "Engine/SpecularProfile.h"
#include "SceneCaptureRendering.h"
#include "NaniteSceneProxy.h"
#include "Nanite/NaniteRayTracing.h"
#include "Nanite/NaniteComposition.h"
#include "Nanite/Voxel.h"
#include "Nanite/NaniteShading.h"
#include "RayTracing/RayTracingInstanceCulling.h"
#include "GPUMessaging.h"
#include "RectLightTextureManager.h"
#include "IESTextureManager.h"
#include "Lumen/LumenFrontLayerTranslucency.h"
#include "Lumen/LumenSceneLighting.h"
#include "Containers/ChunkedArray.h"
#include "Async/ParallelFor.h"
#include "Shadows/ShadowSceneRenderer.h"
#include "HeterogeneousVolumes/HeterogeneousVolumes.h"
#include "ComponentRecreateRenderStateContext.h"
#include "RenderCore.h"
#include "VariableRateShadingImageManager.h"
#include "LocalFogVolumeRendering.h"
#include "Shadows/ShadowScene.h"
#include "Lumen/LumenHardwareRayTracingCommon.h"
#include "SparseVolumeTexture/ISparseVolumeTextureStreamingManager.h"
#include "WaterInfoTextureRendering.h"
#include "PostProcess/DebugAlphaChannel.h"
#include "MegaLights/MegaLights.h"
#include "Rendering/CustomRenderPass.h"
#include "CustomRenderPassSceneCapture.h"
#include "EnvironmentComponentsFlags.h"
#include "GenerateMips.h"
#include "Froxel/Froxel.h"

#if !UE_BUILD_SHIPPING
#include "RenderCaptureInterface.h"
#endif

extern int32 GNaniteShowStats;
extern int32 GNanitePickingDomain;

extern DynamicRenderScaling::FBudget GDynamicNaniteScalingPrimary;

static TAutoConsoleVariable<int32> CVarClearCoatNormal(
	TEXT("r.ClearCoatNormal"),
	0,
	TEXT("0 to disable clear coat normal.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarIrisNormal(
	TEXT("r.IrisNormal"),
	0,
	TEXT("0 to disable iris normal.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_ReadOnly);

int32 GbEnableAsyncComputeTranslucencyLightingVolumeClear = 0; // @todo: disabled due to GPU crashes
static FAutoConsoleVariableRef CVarEnableAsyncComputeTranslucencyLightingVolumeClear(
	TEXT("r.EnableAsyncComputeTranslucencyLightingVolumeClear"),
	GbEnableAsyncComputeTranslucencyLightingVolumeClear,
	TEXT("Whether to clear the translucency lighting volume using async compute.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability
);

#if !UE_BUILD_SHIPPING

static int32 GCaptureNextDeferredShadingRendererFrame = -1;
static FAutoConsoleVariableRef CVarCaptureNextRenderFrame(
	TEXT("r.CaptureNextDeferredShadingRendererFrame"),
	GCaptureNextDeferredShadingRendererFrame,
	TEXT("0 to capture the immideately next frame using e.g. RenderDoc or PIX.\n")
	TEXT(" > 0: N frames delay\n")
	TEXT(" < 0: disabled"),
	ECVF_RenderThreadSafe);

#endif

static TAutoConsoleVariable<int32> CVarRayTracing(
	TEXT("r.RayTracing"),
	0,
	TEXT("0 to disable ray tracing.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

int32 GRayTracingUseTextureLod = 0;
static TAutoConsoleVariable<int32> CVarRayTracingTextureLod(
	TEXT("r.RayTracing.UseTextureLod"),
	GRayTracingUseTextureLod,
	TEXT("Enable automatic texture mip level selection in ray tracing material shaders.\n")
	TEXT(" 0: highest resolution mip level is used for all texture (default).\n")
	TEXT(" 1: texture LOD is approximated based on total ray length, output resolution and texel density at hit point (ray cone method)."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);

static int32 GForceAllRayTracingEffects = -1;
static TAutoConsoleVariable<int32> CVarForceAllRayTracingEffects(
	TEXT("r.RayTracing.ForceAllRayTracingEffects"),
	GForceAllRayTracingEffects,
	TEXT("Force all ray tracing effects ON/OFF.\n")
	TEXT(" -1: Do not force (default) \n")
	TEXT(" 0: All ray tracing effects disabled\n")
	TEXT(" 1: All ray tracing effects enabled"),
	ECVF_RenderThreadSafe);

static int32 GRayTracingAllowInline = 1;
static TAutoConsoleVariable<int32> CVarRayTracingAllowInline(
	TEXT("r.RayTracing.AllowInline"),
	GRayTracingAllowInline,
	TEXT("Allow use of Inline Ray Tracing if supported (default=1)."),	
	ECVF_RenderThreadSafe);

static int32 GRayTracingAllowPipeline = 1;
static TAutoConsoleVariable<int32> CVarRayTracingAllowPipeline(
	TEXT("r.RayTracing.AllowPipeline"),
	GRayTracingAllowPipeline,
	TEXT("Allow use of Ray Tracing pipelines if supported (default=1)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingAsyncBuild(
	TEXT("r.RayTracing.AsyncBuild"),
	0,
	TEXT("Whether to build ray tracing acceleration structures on async compute queue.\n"),
	ECVF_RenderThreadSafe
);

static int32 GRayTracingMultiGpuTLASMask = 1;
static FAutoConsoleVariableRef CVarRayTracingMultiGpuTLASMask(
	TEXT("r.RayTracing.MultiGpuMaskTLAS"),
	GRayTracingMultiGpuTLASMask,
	TEXT("For Multi-GPU, controls which GPUs TLAS and material pipeline updates run on.  (default = 1)\n")
	TEXT(" 0: Run TLAS and material pipeline updates on all GPUs.  Original behavior, which may be useful for debugging.\n")
	TEXT(" 1: Run TLAS and material pipeline updates masked to the active view's GPUs to improve performance.  BLAS updates still run on all GPUs.")
);

static TAutoConsoleVariable<int32> CVarSceneDepthHZBAsyncCompute(
	TEXT("r.SceneDepthHZBAsyncCompute"), 0,
	TEXT("Selects whether HZB for scene depth buffer should be built with async compute.\n")
	TEXT(" 0: Don't use async compute (default)\n")
	TEXT(" 1: Use async compute, start as soon as possible\n")
	TEXT(" 2: Use async compute, start after ComputeLightGrid.CompactLinks pass"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarShadowMapsRenderEarly(
	TEXT("r.shadow.ShadowMapsRenderEarly"), 0,
	TEXT("If enabled, shadows will render earlier in the frame. This can help async compute scheduling on some platforms\n")
	TEXT("Note: This is not compatible with VSMs\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTranslucencyVelocity(
	TEXT("r.Translucency.Velocity"), 1,
	TEXT("Whether translucency can draws depth/velocity (enabled by default)"),
	ECVF_RenderThreadSafe);

static FAutoConsoleCommand RecreateRenderStateContextCmd(
	TEXT("r.RecreateRenderStateContext"),
	TEXT("Recreate render state."),
	FConsoleCommandDelegate::CreateStatic([] { FGlobalComponentRecreateRenderStateContext Context; }));

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarForceBlackVelocityBuffer(
	TEXT("r.Test.ForceBlackVelocityBuffer"), 0,
	TEXT("Force the velocity buffer to have no motion vector for debugging purpose."),
	ECVF_RenderThreadSafe);
#endif

static TAutoConsoleVariable<int32> CVarNaniteViewMeshLODBiasEnable(
	TEXT("r.Nanite.ViewMeshLODBias.Enable"), 1,
	TEXT("Whether LOD offset to apply for rasterized Nanite meshes for the main viewport should be based off TSR's ScreenPercentage (Enabled by default)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNaniteViewMeshLODBiasOffset(
	TEXT("r.Nanite.ViewMeshLODBias.Offset"), 0.0f,
	TEXT("LOD offset to apply for rasterized Nanite meshes for the main viewport when using TSR (Default = 0)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarNaniteViewMeshLODBiasMin(
	TEXT("r.Nanite.ViewMeshLODBias.Min"), -2.0f,
	TEXT("Minimum LOD offset for rasterizing Nanite meshes for the main viewport (Default = -2)."),
	ECVF_RenderThreadSafe);

namespace Lumen
{
	extern bool AnyLumenHardwareRayTracingPassEnabled();
}
namespace Nanite
{
	extern bool IsStatFilterActive(const FString& FilterName);
	extern void ListStatFilters(FSceneRenderer* SceneRenderer);
}
extern bool ShouldVisualizeLightGrid();

DECLARE_CYCLE_STAT(TEXT("InitViews Intentional Stall"), STAT_InitViews_Intentional_Stall, STATGROUP_InitViews);

DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer UpdateDownsampledDepthSurface"), STAT_FDeferredShadingSceneRenderer_UpdateDownsampledDepthSurface, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Render Init"), STAT_FDeferredShadingSceneRenderer_Render_Init, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PreRender"), STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AllocGBufferTargets"), STAT_FDeferredShadingSceneRenderer_AllocGBufferTargets, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer DBuffer"), STAT_FDeferredShadingSceneRenderer_DBuffer, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer ResolveDepth After Basepass"), STAT_FDeferredShadingSceneRenderer_ResolveDepth_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Resolve After Basepass"), STAT_FDeferredShadingSceneRenderer_Resolve_After_Basepass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer FXSystem PostRenderOpaque"), STAT_FDeferredShadingSceneRenderer_FXSystem_PostRenderOpaque, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer AfterBasePass"), STAT_FDeferredShadingSceneRenderer_AfterBasePass, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer Lighting"), STAT_FDeferredShadingSceneRenderer_Lighting, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftOcclusion"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderSkyAtmosphere"), STAT_FDeferredShadingSceneRenderer_RenderSkyAtmosphere, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFog"), STAT_FDeferredShadingSceneRenderer_RenderFog, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLocalFogVolume"), STAT_FDeferredShadingSceneRenderer_RenderLocalFogVolume, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderLightShaftBloom"), STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("DeferredShadingSceneRenderer RenderFinish"), STAT_FDeferredShadingSceneRenderer_RenderFinish, STATGROUP_SceneRendering);

DECLARE_GPU_STAT(RayTracingScene);
DECLARE_GPU_STAT(RayTracingGeometry);

DEFINE_GPU_STAT(Postprocessing);
DECLARE_GPU_STAT(VisibilityCommands);
DECLARE_GPU_STAT(RenderDeferredLighting);
DECLARE_GPU_STAT(AllocateRendertargets);
DECLARE_GPU_STAT(FrameRenderFinish);
DECLARE_GPU_STAT(SortLights);
DECLARE_GPU_STAT(PostRenderOpsFX);
DECLARE_GPU_STAT_NAMED(Unaccounted, TEXT("[unaccounted]"));
DECLARE_GPU_STAT(WaterRendering);
DECLARE_GPU_STAT(HairRendering);
DEFINE_GPU_DRAWCALL_STAT(VirtualTextureUpdate);
DECLARE_GPU_STAT(UploadDynamicBuffers);
DECLARE_GPU_STAT(PostOpaqueExtensions);
DEFINE_GPU_STAT(CustomRenderPasses);

DECLARE_GPU_STAT_NAMED(NaniteVisBuffer, TEXT("Nanite VisBuffer"));

DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Total Raster Bins"), STAT_NaniteBasePassTotalRasterBins, STATGROUP_Nanite);
DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Visible Raster Bins"), STAT_NaniteBasePassVisibleRasterBins, STATGROUP_Nanite);

DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Total Shading Bins"), STAT_NaniteBasePassTotalShadingBins, STATGROUP_Nanite);
DECLARE_DWORD_COUNTER_STAT(TEXT("BasePass Visible Shading Bins"), STAT_NaniteBasePassVisibleShadingBins, STATGROUP_Nanite);

CSV_DEFINE_CATEGORY(LightCount, true);

/*-----------------------------------------------------------------------------
	Global Illumination Plugin Function Delegates
-----------------------------------------------------------------------------*/

static FGlobalIlluminationPluginDelegates::FAnyRayTracingPassEnabled GIPluginAnyRaytracingPassEnabledDelegate;
FGlobalIlluminationPluginDelegates::FAnyRayTracingPassEnabled& FGlobalIlluminationPluginDelegates::AnyRayTracingPassEnabled()
{
	return GIPluginAnyRaytracingPassEnabledDelegate;
}

static FGlobalIlluminationPluginDelegates::FPrepareRayTracing GIPluginPrepareRayTracingDelegate;
FGlobalIlluminationPluginDelegates::FPrepareRayTracing& FGlobalIlluminationPluginDelegates::PrepareRayTracing()
{
	return GIPluginPrepareRayTracingDelegate;
}

static FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectLight GIPluginRenderDiffuseIndirectLightDelegate;
FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectLight& FGlobalIlluminationPluginDelegates::RenderDiffuseIndirectLight()
{
	return GIPluginRenderDiffuseIndirectLightDelegate;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectVisualizations GIPluginRenderDiffuseIndirectVisualizationsDelegate;
FGlobalIlluminationPluginDelegates::FRenderDiffuseIndirectVisualizations& FGlobalIlluminationPluginDelegates::RenderDiffuseIndirectVisualizations()
{
	return GIPluginRenderDiffuseIndirectVisualizationsDelegate;
}
#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)

const TCHAR* GetDepthPassReason(bool bDitheredLODTransitionsUseStencil, EShaderPlatform ShaderPlatform)
{
	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		return TEXT("(Forced by ForwardShading)");
	}

	if (UseNanite(ShaderPlatform))
	{
		return TEXT("(Forced by Nanite)");
	}

	if (IsUsingDBuffers(ShaderPlatform))
	{
		return TEXT("(Forced by DBuffer)");
	}

	if (UseVirtualTexturing(ShaderPlatform))
	{
		return TEXT("(Forced by VirtualTexture)");
	}

	if (bDitheredLODTransitionsUseStencil)
	{
		return TEXT("(Forced by StencilLODDither)");
	}

	return TEXT("");
}

/*-----------------------------------------------------------------------------
	FDeferredShadingSceneRenderer
-----------------------------------------------------------------------------*/

FDeferredShadingSceneRenderer::FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer)
	: FSceneRenderer(InViewFamily, HitProxyConsumer)
	, DepthPass(GetDepthPassInfo(Scene))
	, SceneCullingRenderer(*Scene->SceneCulling, *this)
	, bAreLightsInLightGrid(false)
{
	ViewPipelineStates.SetNum(AllViews.Num());

	ShadowSceneRenderer = MakeUnique<FShadowSceneRenderer>(*this);
}

/** 
* Renders the view family. 
*/
DECLARE_CYCLE_STAT(TEXT("Wait RayTracing Add Mesh Batch"), STAT_WaitRayTracingAddMesh, STATGROUP_SceneRendering);
DECLARE_CYCLE_STAT(TEXT("Wait Ray Tracing Scene Initialization"), STAT_WaitRayTracingSceneInitTask, STATGROUP_SceneRendering);

/**
 * Returns true if the depth Prepass needs to run
 */
bool FDeferredShadingSceneRenderer::ShouldRenderPrePass() const
{
	return (DepthPass.EarlyZPassMode != DDM_None || DepthPass.bEarlyZPassMovable != 0);
}

/**
 * Returns true if the Nanite rendering needs to run
 */
bool FDeferredShadingSceneRenderer::ShouldRenderNanite() const
{
	return UseNanite(ShaderPlatform) && ViewFamily.EngineShowFlags.NaniteMeshes && Nanite::GStreamingManager.HasResourceEntries();
}

bool FDeferredShadingSceneRenderer::RenderHzb(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture, const FBuildHZBAsyncComputeParams* AsyncComputeParams, Froxel::FRenderer& FroxelRenderer)
{
	RDG_EVENT_SCOPE_STAT(GraphBuilder, HZB, "HZB");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HZB);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		FSceneViewState* ViewState = View.ViewState;
		const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);


		if (ViewPipelineState.bClosestHZB || ViewPipelineState.bFurthestHZB)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "BuildHZB(ViewId=%d)", ViewIndex);

			FRDGTextureRef ClosestHZBTexture = nullptr;
			FRDGTextureRef FurthestHZBTexture = nullptr;

			BuildHZB(
				GraphBuilder,
				SceneDepthTexture,
				/* VisBufferTexture = */ nullptr,
				View.ViewRect,
				View.GetFeatureLevel(),
				View.GetShaderPlatform(),
				TEXT("HZBClosest"),
				/* OutClosestHZBTexture = */ ViewPipelineState.bClosestHZB ? &ClosestHZBTexture : nullptr,
				TEXT("HZBFurthest"),
				/* OutFurthestHZBTexture = */ &FurthestHZBTexture,
				BuildHZBDefaultPixelFormat,
				AsyncComputeParams,
				FroxelRenderer.GetView(ViewIndex));

			// Update the view.
			{
				View.HZBMipmap0Size = FurthestHZBTexture->Desc.Extent;
				View.HZB = FurthestHZBTexture;

				// Extract furthest HZB texture.
				if (View.ViewState)
				{
					if (ShouldRenderNanite() || FInstanceCullingContext::IsOcclusionCullingEnabled())
					{
						GraphBuilder.QueueTextureExtraction(FurthestHZBTexture, &View.ViewState->PrevFrameViewInfo.HZB);
					}
					else
					{
						View.ViewState->PrevFrameViewInfo.HZB = nullptr;
					}
				}

				// Extract closest HZB texture.
				if (ViewPipelineState.bClosestHZB)
				{
					View.ClosestHZB = ClosestHZBTexture;
				}
			}
		}

		if (FamilyPipelineState->bHZBOcclusion && ViewState && ViewState->HZBOcclusionTests.GetNum() != 0)
		{
			check(ViewState->HZBOcclusionTests.IsValidFrame(ViewState->OcclusionFrameCounter));
			ViewState->HZBOcclusionTests.Submit(GraphBuilder, View);
		}

		if (Scene->InstanceCullingOcclusionQueryRenderer && View.ViewState)
		{
			// Render per-instance occlusion queries and save the mask to interpret results on the next frame
			const uint32 OcclusionQueryMaskForThisView = Scene->InstanceCullingOcclusionQueryRenderer->Render(GraphBuilder, Scene->GPUScene, View);
			View.ViewState->PrevFrameViewInfo.InstanceOcclusionQueryMask = OcclusionQueryMaskForThisView;
		}
	}

	return FamilyPipelineState->bHZBOcclusion;
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderOpaqueFXPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
END_SHADER_PARAMETER_STRUCT()

static void RenderOpaqueFX(
	FRDGBuilder& GraphBuilder,
	TConstStridedView<FSceneView> Views,
	FSceneUniformBuffer &SceneUniformBuffer,
	FFXSystemInterface* FXSystem,
	ERHIFeatureLevel::Type FeatureLevel,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer)
{
	// Notify the FX system that opaque primitives have been rendered and we now have a valid depth buffer.
	if (FXSystem && Views.Num() > 0)
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, PostRenderOpsFX, "PostRenderOpsFX");
		RDG_GPU_STAT_SCOPE(GraphBuilder, PostRenderOpsFX);
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderOpaqueFX);

		const ERDGPassFlags UBPassFlags = ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull;

		if (HasRayTracedOverlay(*Views[0].Family))
		{
			// In the case of Path Tracing/RT Debug -- we have not yet written to the SceneColor buffer, so make a dummy set of textures instead
			SceneTexturesUniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, nullptr, FeatureLevel, ESceneTextureSetupMode::SceneVelocity);
		}

		// Add a pass which extracts the RHI handle from the scene textures UB and sends it to the FX system.
		FRenderOpaqueFXPassParameters* ExtractUBPassParameters = GraphBuilder.AllocParameters<FRenderOpaqueFXPassParameters>();
		ExtractUBPassParameters->SceneTextures = SceneTexturesUniformBuffer;
		GraphBuilder.AddPass(RDG_EVENT_NAME("SetSceneTexturesUniformBuffer"), ExtractUBPassParameters, UBPassFlags, [ExtractUBPassParameters, FXSystem](FRHICommandListImmediate&)
		{
			FXSystem->SetSceneTexturesUniformBuffer(ExtractUBPassParameters->SceneTextures->GetRHIRef());
		});

		FXSystem->PostRenderOpaque(GraphBuilder, Views, SceneUniformBuffer, true /*bAllowGPUParticleUpdate*/);

		// Clear the scene textures UB pointer on the FX system. Use the same pass parameters to extend resource lifetimes.
		GraphBuilder.AddPass(RDG_EVENT_NAME("UnsetSceneTexturesUniformBuffer"), ExtractUBPassParameters, UBPassFlags, [FXSystem](FRHICommandListImmediate&)
		{
			FXSystem->SetSceneTexturesUniformBuffer({});
		});

		if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
		{
			GPUSortManager->OnPostRenderOpaque(GraphBuilder);
		}
	}
}

#if RHI_RAYTRACING

static bool ShouldPrepareRayTracingDecals(const FScene& Scene, const FSceneViewFamily& ViewFamily)
{
	if (!IsRayTracingEnabled() || !RHISupportsRayTracingCallableShaders(ViewFamily.GetShaderPlatform()))
	{
		return false;
	}

	if (Scene.Decals.Num() == 0 || RayTracing::ShouldExcludeDecals())
	{
		return false;
	}

	return ViewFamily.EngineShowFlags.PathTracing && PathTracing::UsesDecals(ViewFamily);
}

static void DeduplicateRayGenerationShaders(TArray< FRHIRayTracingShader*>& RayGenShaders)
{
	TSet<FRHIRayTracingShader*> UniqueRayGenShaders;
	for (FRHIRayTracingShader* Shader : RayGenShaders)
	{
		UniqueRayGenShaders.Add(Shader);
	}
	RayGenShaders = UniqueRayGenShaders.Array();
}

BEGIN_SHADER_PARAMETER_STRUCT(FBuildAccelerationStructurePassParams, )
	RDG_BUFFER_ACCESS(DynamicGeometryScratchBuffer, ERHIAccess::UAVCompute)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRayTracingLightGrid, LightGridPacked)
	SHADER_PARAMETER_STRUCT_REF(FLumenHardwareRayTracingUniformBufferParameters, LumenHardwareRayTracingUniformBuffer)

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, RayTracingDataBuffer)
END_SHADER_PARAMETER_STRUCT()

bool FDeferredShadingSceneRenderer::SetupRayTracingPipelineStatesAndSBT(FRDGBuilder& GraphBuilder, bool bAnyLumenHardwareInlineRayTracingPassEnabled)
{
	if (!IsRayTracingEnabled() || Views.Num() == 0)
	{
		return false;
	}

	if (!bAnyRayTracingPassEnabled)
	{
		return false;
	}	

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::SetupRayTracingPipelineStatesAndSBT);

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	if (ReferenceView.AddRayTracingMeshBatchTaskList.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingAddMesh);

		UE::Tasks::Wait(ReferenceView.AddRayTracingMeshBatchTaskList);

		for (int32 TaskIndex = 0; TaskIndex < ReferenceView.AddRayTracingMeshBatchTaskList.Num(); TaskIndex++)
		{
			ReferenceView.DirtyRayTracingShaderBindings.Append(*ReferenceView.DirtyRayTracingShaderBindingsPerTask[TaskIndex]);
		}

		ReferenceView.AddRayTracingMeshBatchTaskList.Empty();
	}

	if (!GRHISupportsRayTracingShaders && !GRHISupportsInlineRayTracing)
	{
		return false;
	}

	const bool bIsPathTracing = ViewFamily.EngineShowFlags.PathTracing;

	{
		// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. 
		// Should register each effect at startup and just loop over them automatically to gather all required shaders.

		TArray<FRHIRayTracingShader*> RayGenShaders;

		// We typically see ~120 raygen shaders, but allow some headroom to avoid reallocation if our estimate is wrong.
		RayGenShaders.Reserve(256);

		if (bIsPathTracing)
		{
			// This view only needs the path tracing raygen shaders as all other
			// passes should be disabled.
			PreparePathTracing(ViewFamily, *Scene, RayGenShaders);
		}
		else
		{
			// Path tracing is disabled, get all other possible raygen shaders
			PrepareRayTracingDebug(ViewFamily, RayGenShaders);

			// These other cases do potentially depend on the camera position since they are
			// driven by FinalPostProcessSettings, which is why we need to merge them across views
			if (!IsForwardShadingEnabled(ShaderPlatform))
			{
				for (const FViewInfo& View : Views)
				{
					PrepareRayTracingShadows(View, *Scene, RayGenShaders);
					PrepareRayTracingAmbientOcclusion(View, RayGenShaders);
					PrepareRayTracingSkyLight(View, *Scene, RayGenShaders);
					PrepareRayTracingGlobalIlluminationPlugin(View, RayGenShaders);
					PrepareRayTracingTranslucency(View, RayGenShaders);
					PrepareRayTracingVolumetricFogShadows(View, *Scene, RayGenShaders);

					if (DoesPlatformSupportLumenGI(ShaderPlatform) && Lumen::UseHardwareRayTracing(ViewFamily))
					{
						PrepareLumenHardwareRayTracingScreenProbeGather(View, RayGenShaders);
						PrepareLumenHardwareRayTracingShortRangeAO(View, RayGenShaders);
						PrepareLumenHardwareRayTracingRadianceCache(View, RayGenShaders);
						PrepareLumenHardwareRayTracingReflections(View, RayGenShaders);
						PrepareLumenHardwareRayTracingReSTIR(View, RayGenShaders);
						PrepareLumenHardwareRayTracingVisualize(View, RayGenShaders);
					}

					PrepareMegaLightsHardwareRayTracing(View, RayGenShaders);
				}
			}
			DeduplicateRayGenerationShaders(RayGenShaders);
		}

		if (RayGenShaders.Num())
		{
			// Create RTPSO and kick off high-level material parameter binding tasks which will be consumed during RDG execution in BindRayTracingMaterialPipeline()			
			uint32 MaxLocalBindingDataSize = 0;
			CreateRayTracingMaterialPipeline(GraphBuilder, ReferenceView, RayGenShaders, MaxLocalBindingDataSize);
			
			const FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
			ReferenceView.RayTracingSBT = Scene->RayTracingSBT.AllocateRHI(GraphBuilder.RHICmdList, ERayTracingShaderBindingMode::RTPSO, ERayTracingHitGroupIndexingMode::Allow, RayTracingScene.NumMissShaderSlots, RayTracingScene.NumCallableShaderSlots, MaxLocalBindingDataSize);
		}
	}

	// Add Lumen hardware ray tracing materials
	if (!bIsPathTracing)
	{
		TArray<FRHIRayTracingShader*> LumenHardwareRayTracingRayGenShaders;

		if (DoesPlatformSupportLumenGI(ShaderPlatform))
		{
			for (const FViewInfo& View : Views)
			{
				PrepareLumenHardwareRayTracingVisualizeLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingTranslucencyVolumeLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingRadiosityLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingReflectionsLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingReSTIRLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingScreenProbeGatherLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
				PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
			}
		}

		for (const FViewInfo& View : Views)
		{
			PrepareMegaLightsHardwareRayTracingLumenMaterial(View, LumenHardwareRayTracingRayGenShaders);
		}

		DeduplicateRayGenerationShaders(LumenHardwareRayTracingRayGenShaders);
		
		uint32 MaxLocalBindingDataSize = 0;
		ERayTracingShaderBindingMode ShaderBindingMode = (bAnyLumenHardwareInlineRayTracingPassEnabled && GRHIGlobals.RayTracing.RequiresInlineRayTracingSBT) ? 
			ERayTracingShaderBindingMode::Inline : ERayTracingShaderBindingMode::Disabled;
		if (LumenHardwareRayTracingRayGenShaders.Num())
		{
			CreateLumenHardwareRayTracingMaterialPipeline(GraphBuilder, ReferenceView, LumenHardwareRayTracingRayGenShaders, MaxLocalBindingDataSize);
			EnumAddFlags(ShaderBindingMode, ERayTracingShaderBindingMode::RTPSO);
		}

		if (ShaderBindingMode != ERayTracingShaderBindingMode::Disabled)
		{
			SetupLumenHardwareRaytracingHitGroupBindings(GraphBuilder, ReferenceView, ShaderBindingMode);

			// Allocate the SBT if using hit shaders or the RHI requires an SBT for inline raytracing
			const FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
			ReferenceView.LumenHardwareRayTracingSBT = Scene->RayTracingSBT.AllocateRHI(GraphBuilder.RHICmdList, ShaderBindingMode, ERayTracingHitGroupIndexingMode::Allow, RayTracingScene.NumMissShaderSlots, RayTracingScene.NumCallableShaderSlots, MaxLocalBindingDataSize);
		}
	}

	// Initialize common resources used for lighting in ray tracing effects
	for (int32 ViewIndex = 0; ViewIndex < AllFamilyViews.Num(); ++ViewIndex)
	{
		// TODO:  It would make more sense for common ray tracing resources to be in a shared structure, rather than copied into each FViewInfo.
		//        A goal is to have the FViewInfo structure only be visible to the scene renderer that owns it, to avoid dependencies being created
		//        that could lead to maintenance issues or interfere with parallelism goals.  For now, this works though...
		FViewInfo* View = const_cast<FViewInfo*>(static_cast<const FViewInfo*>(AllFamilyViews[ViewIndex]));

		// Send common ray tracing resources from reference view to all others.
		if (View->bHasAnyRayTracingPass && View != &ReferenceView)
		{
			View->RayTracingMaterialPipeline = ReferenceView.RayTracingMaterialPipeline;
			View->RayTracingSBT = ReferenceView.RayTracingSBT;

			View->LumenHardwareRayTracingMaterialPipeline = ReferenceView.LumenHardwareRayTracingMaterialPipeline;
			View->LumenHardwareRayTracingSBT = ReferenceView.LumenHardwareRayTracingSBT;
		}
	}

	return true;
}

void FDeferredShadingSceneRenderer::SetupRayTracingLightDataForViews(FRDGBuilder& GraphBuilder)
{
	if (!bAnyRayTracingPassEnabled)
	{
		return;
	}

	const bool bPathTracingEnabled = ViewFamily.EngineShowFlags.PathTracing && FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(Scene->GetShaderPlatform());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		bool bBuildLightGrid = false;

		// Path Tracing currently uses its own code to manage lights, so doesn't need to run this.
		if (!bPathTracingEnabled)
		{
			if (Lumen::IsUsingRayTracingLightingGrid(ViewFamily, View, GetViewPipelineState(View).DiffuseIndirectMethod)
				|| GetRayTracingTranslucencyOptions(View).bEnabled
				|| ViewFamily.EngineShowFlags.RayTracingDebug)
			{
				bBuildLightGrid = true;
			}
		}

		// The light data is built in TranslatedWorld space so must be built per view
		View.RayTracingLightGridUniformBuffer = CreateRayTracingLightData(GraphBuilder, Scene, View, View.ShaderMap, bBuildLightGrid);
	}
}

bool FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates(FRDGBuilder& GraphBuilder, FRDGBufferRef& OutDynamicGeometryScratchBuffer)
{
	OutDynamicGeometryScratchBuffer = nullptr;

	// We only need to update ray tracing scene for the first view family, if multiple are rendered in a single scene render call.
	if (!bShouldUpdateRayTracingScene)
	{
		// - Nanite ray tracing instances are already pointing at the new BLASes and RayTracingDataOffsets in GPUScene have been updated
		Nanite::GRayTracingManager.ProcessBuildRequests(GraphBuilder);
		return false;
	}

	check(IsRayTracingEnabled() && bAnyRayTracingPassEnabled && !Views.IsEmpty());

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::DispatchRayTracingWorldUpdates);

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];

	{
		SCOPE_CYCLE_COUNTER(STAT_WaitRayTracingSceneInitTask);
		ReferenceView.RayTracingSceneInitTask.Wait();
	}

	const bool bRayTracingAsyncBuild = CVarRayTracingAsyncBuild.GetValueOnRenderThread() != 0 && GRHISupportsRayTracingAsyncBuildAccelerationStructure;
	const ERDGPassFlags ComputePassFlags = bRayTracingAsyncBuild ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute;

	// Make sure there are no pending skin cache builds and updates anymore:
	// FSkeletalMeshObjectGPUSkin::UpdateDynamicData_RenderThread could have enqueued build operations which might not have
	// been processed by CommitRayTracingGeometryUpdates. 
	// All pending builds should be done before adding them to the top level BVH.
	if (FRayTracingSkinnedGeometryUpdateQueue* RayTracingSkinnedGeometryUpdateQueue = Scene->GetRayTracingSkinnedGeometryUpdateQueue())
	{
		RayTracingSkinnedGeometryUpdateQueue->Commit(GraphBuilder, ComputePassFlags);
	}
	FRayTracingScene& RayTracingScene = Scene->RayTracingScene;

	if (RayTracingScene.GeometriesToBuild.Num() > 0)
	{
		// Force update all the collected geometries (use stack allocator?)
		GRayTracingGeometryManager->ForceBuildIfPending(GraphBuilder.RHICmdList, RayTracingScene.GeometriesToBuild);
	}

	{
		Nanite::GRayTracingManager.ProcessUpdateRequests(GraphBuilder, GetSceneUniforms());
		const bool bAnyBlasRebuilt = Nanite::GRayTracingManager.ProcessBuildRequests(GraphBuilder);
		if (bAnyBlasRebuilt)
		{
			for (FViewInfo& View : Views)
			{
				if (View.ViewState != nullptr && !View.bIsOfflineRender)
				{
					// don't invalidate in the offline case because we only get one attempt at rendering each sample
					View.ViewState->PathTracingInvalidate();
				}
			}
		}
	}

	// Keep mask the same as what's already set (which will be the view mask) if TLAS updates should be masked to the view
	RDG_GPU_MASK_SCOPE(GraphBuilder, GRayTracingMultiGpuTLASMask ? GraphBuilder.RHICmdList.GetGPUMask() : FRHIGPUMask::All());

	Scene->GetRayTracingDynamicGeometryCollection()->AddDynamicGeometryUpdatePass(ReferenceView, GraphBuilder, ComputePassFlags, OutDynamicGeometryScratchBuffer);


	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, RayTracingScene, "RayTracingScene");
		RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingScene);
		RayTracingScene.Create(GraphBuilder, ReferenceView, &Scene->GPUScene, ComputePassFlags);
		RayTracingScene.Build(GraphBuilder, ComputePassFlags | ERDGPassFlags::NeverCull, OutDynamicGeometryScratchBuffer);
	}

	GraphBuilder.AddDispatchHint();

	return true;
}

void FDeferredShadingSceneRenderer::WaitForRayTracingScene(FRDGBuilder& GraphBuilder)
{
	check(bAnyRayTracingPassEnabled);

	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::WaitForRayTracingScene);

	// Keep mask the same as what's already set (which will be the view mask) if TLAS updates should be masked to the view
	RDG_GPU_MASK_SCOPE(GraphBuilder, GRayTracingMultiGpuTLASMask ? GraphBuilder.RHICmdList.GetGPUMask() : FRHIGPUMask::All());

	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];
	
	SetupLumenHardwareRayTracingUniformBuffer(ReferenceView);

	// Send ray tracing resources from reference view to all others.
	for (int32 ViewIndex = 0; ViewIndex < AllFamilyViews.Num(); ++ViewIndex)
	{
		// See comment above where we copy "RayTracingSubSurfaceProfileTexture" to each view...
		FViewInfo* View = const_cast<FViewInfo*>(static_cast<const FViewInfo*>(AllFamilyViews[ViewIndex]));
		if (View->bHasAnyRayTracingPass && View != &ReferenceView)
		{
			View->LumenHardwareRayTracingMaterialPipeline = ReferenceView.LumenHardwareRayTracingMaterialPipeline;
			View->LumenHardwareRayTracingUniformBuffer = ReferenceView.LumenHardwareRayTracingUniformBuffer;
		}
	}
	
	bool bAnyLumenHardwareInlineRayTracingPassEnabled = false;
	for (const FViewInfo& View : Views)
	{
		if (Lumen::AnyLumenHardwareInlineRayTracingPassEnabled(Scene, View) 
			|| MegaLights::UseInlineHardwareRayTracing(ViewFamily))
		{
			bAnyLumenHardwareInlineRayTracingPassEnabled = true;
		}
	}

	SetupRayTracingPipelineStatesAndSBT(GraphBuilder, bAnyLumenHardwareInlineRayTracingPassEnabled);
	
	if (bAnyLumenHardwareInlineRayTracingPassEnabled)
	{
		SetupLumenHardwareRayTracingHitGroupBuffer(GraphBuilder, ReferenceView);
	}

	const bool bIsPathTracing = ViewFamily.EngineShowFlags.PathTracing;

	FBuildAccelerationStructurePassParams* PassParams = GraphBuilder.AllocParameters<FBuildAccelerationStructurePassParams>();
	PassParams->Scene = GetSceneUniformBufferRef(GraphBuilder);
	PassParams->DynamicGeometryScratchBuffer = nullptr;
	PassParams->LightGridPacked = bIsPathTracing ? nullptr : ReferenceView.RayTracingLightGridUniformBuffer; // accessed by FRayTracingLightingMS // Is this needed for anything?
	PassParams->LumenHardwareRayTracingUniformBuffer = ReferenceView.LumenHardwareRayTracingUniformBuffer;

	const bool bShouldRenderNanite = ShouldRenderNanite();

	if (bShouldRenderNanite)
	{
		PassParams->ClusterPageData = Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParams->HierarchyBuffer = Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);
		PassParams->RayTracingDataBuffer = Nanite::GRayTracingManager.GetAuxiliaryDataSRV(GraphBuilder);
	}
	else
	{
		PassParams->ClusterPageData = nullptr;
		PassParams->HierarchyBuffer = nullptr;
		PassParams->RayTracingDataBuffer = nullptr;
	}

	const FRayTracingLightFunctionMap* RayTracingLightFunctionMap = GraphBuilder.Blackboard.Get<FRayTracingLightFunctionMap>();
	GraphBuilder.AddPass(RDG_EVENT_NAME("SetRayTracingBindings"), PassParams, ERDGPassFlags::Copy | ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[this, PassParams, bIsPathTracing, &ReferenceView, RayTracingLightFunctionMap, bShouldRenderNanite](FRDGAsyncTask, FRHICommandList& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetRayTracingBindings);

		if (bShouldRenderNanite)
		{
			FNaniteRayTracingUniformParameters NaniteRayTracingUniformParams;
			NaniteRayTracingUniformParams.PageConstants.X = Scene->GPUScene.InstanceSceneDataSOAStride;
			NaniteRayTracingUniformParams.PageConstants.Y = Nanite::GStreamingManager.GetMaxStreamingPages();
			NaniteRayTracingUniformParams.MaxNodes = Nanite::FGlobalResources::GetMaxNodes();
			NaniteRayTracingUniformParams.MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
			NaniteRayTracingUniformParams.RenderFlags = 0;
			NaniteRayTracingUniformParams.RayTracingCutError = Nanite::GRayTracingManager.GetCutError();
			NaniteRayTracingUniformParams.ClusterPageData = PassParams->ClusterPageData->GetRHI();
			NaniteRayTracingUniformParams.HierarchyBuffer = PassParams->HierarchyBuffer->GetRHI();
			NaniteRayTracingUniformParams.RayTracingDataBuffer = PassParams->RayTracingDataBuffer->GetRHI();

			Nanite::GRayTracingManager.GetUniformBuffer().UpdateUniformBufferImmediate(RHICmdList, NaniteRayTracingUniformParams);
		}

		check(ReferenceView.RayTracingMaterialPipeline || ReferenceView.RayTracingMaterialBindings.Num() == 0);

		if (ReferenceView.RayTracingMaterialPipeline && (ReferenceView.RayTracingMaterialBindings.Num() || ReferenceView.RayTracingCallableBindings.Num()))
		{
			BindRayTracingMaterialPipeline(RHICmdList, ReferenceView);

			if (bIsPathTracing)
			{
				SetupPathTracingDefaultMissShader(RHICmdList, ReferenceView);

				BindLightFunctionShadersPathTracing(RHICmdList, Scene, RayTracingLightFunctionMap, ReferenceView);
			}
			else
			{
				SetupRayTracingDefaultMissShader(RHICmdList, ReferenceView);
				SetupRayTracingLightingMissShader(RHICmdList, ReferenceView);

				BindLightFunctionShaders(RHICmdList, Scene, RayTracingLightFunctionMap, ReferenceView);
			}

			RHICmdList.CommitShaderBindingTable(ReferenceView.RayTracingSBT);
		}

		if (!bIsPathTracing)
		{
			if (GRHISupportsRayTracingShaders || GRHISupportsInlineRayTracing)
			{
				if (ReferenceView.LumenHardwareRayTracingMaterialPipeline)
				{
					RHICmdList.SetRayTracingMissShader(ReferenceView.LumenHardwareRayTracingSBT, RAY_TRACING_MISS_SHADER_SLOT_DEFAULT, ReferenceView.LumenHardwareRayTracingMaterialPipeline, 0 /* MissShaderPipelineIndex */, 0, nullptr, 0);
				}

				if (ReferenceView.LumenHardwareRayTracingSBT)
				{
					BindLumenHardwareRayTracingMaterialPipeline(RHICmdList, ReferenceView);
					RHICmdList.CommitShaderBindingTable(ReferenceView.LumenHardwareRayTracingSBT);
				}
			}
		}
	});
}

#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::BeginInitDynamicShadows(FRDGBuilder& GraphBuilder, FInitViewTaskDatas& TaskDatas, FInstanceCullingManager& InstanceCullingManager)
{
	extern int32 GEarlyInitDynamicShadows;

	// This is called from multiple locations and will succeed if the visibility tasks are ready.
	if (!TaskDatas.DynamicShadows
		&& GEarlyInitDynamicShadows != 0
		&& ViewFamily.EngineShowFlags.DynamicShadows
		&& !ViewFamily.EngineShowFlags.HitProxies
		&& !HasRayTracedOverlay(ViewFamily)
		&& TaskDatas.VisibilityTaskData->IsTaskWaitingAllowed())
	{
		TaskDatas.DynamicShadows = FSceneRenderer::BeginInitDynamicShadows(GraphBuilder, true, TaskDatas.VisibilityTaskData, InstanceCullingManager);
	}
}

void FDeferredShadingSceneRenderer::FinishInitDynamicShadows(FRDGBuilder& GraphBuilder, FDynamicShadowsTaskData*& TaskData, FInstanceCullingManager& InstanceCullingManager)
{
	if (ViewFamily.EngineShowFlags.DynamicShadows && !ViewFamily.EngineShowFlags.HitProxies && !HasRayTracedOverlay(ViewFamily))
	{
		// Setup dynamic shadows.
		if (TaskData)
		{
			FSceneRenderer::FinishInitDynamicShadows(GraphBuilder, TaskData);
		}
		else
		{
			TaskData = InitDynamicShadows(GraphBuilder, InstanceCullingManager);
		}
	}
}

static TAutoConsoleVariable<float> CVarStallInitViews(
	TEXT("CriticalPathStall.AfterInitViews"),
	0.0f,
	TEXT("Sleep for the given time after InitViews. Time is given in ms. This is a debug option used for critical path analysis and forcing a change in the critical path."));

void FDeferredShadingSceneRenderer::CommitFinalPipelineState()
{
	// Family pipeline state
	{
		FamilyPipelineState.Set(&FFamilyPipelineState::bNanite, UseNanite(ShaderPlatform)); // TODO: Should this respect ViewFamily.EngineShowFlags.NaniteMeshes?

		static const auto ICVarHZBOcc = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HZBOcclusion"));
		FamilyPipelineState.Set(&FFamilyPipelineState::bHZBOcclusion, ICVarHZBOcc->GetInt() != 0);	
	}

	CommitIndirectLightingState();

	// Views pipeline states
	for (int32 ViewIndex = 0; ViewIndex < AllViews.Num(); ViewIndex++)
	{
		const FViewInfo& View = *AllViews[ViewIndex];
		TPipelineState<FPerViewPipelineState>& ViewPipelineState = GetViewPipelineStateWritable(View);

		// Commit HZB state
		{
			const bool bHasSSGI = ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::SSGI;
			const bool bUseLumen = ViewPipelineState[&FPerViewPipelineState::DiffuseIndirectMethod] == EDiffuseIndirectMethod::Lumen 
				|| ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::Lumen;

			// Requires FurthestHZB
			ViewPipelineState.Set(&FPerViewPipelineState::bFurthestHZB,
				FamilyPipelineState[&FFamilyPipelineState::bHZBOcclusion] ||
				FamilyPipelineState[&FFamilyPipelineState::bNanite] ||
				ViewPipelineState[&FPerViewPipelineState::AmbientOcclusionMethod] == EAmbientOcclusionMethod::SSAO ||
				ViewPipelineState[&FPerViewPipelineState::ReflectionsMethod] == EReflectionsMethod::SSR ||
				bHasSSGI || bUseLumen);

			ViewPipelineState.Set(&FPerViewPipelineState::bClosestHZB, 
				bHasSSGI || bUseLumen || MegaLights::IsUsingClosestHZB(ViewFamily));
		}
	}

	// Commit all the pipeline states.
	{
		for (int32 ViewIndex = 0; ViewIndex < AllViews.Num(); ViewIndex++)
		{
			const FViewInfo& View = *AllViews[ViewIndex];

			GetViewPipelineStateWritable(View).Commit();
		}
		FamilyPipelineState.Commit();
	} 
}

void FDeferredShadingSceneRenderer::RenderNanite(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& InViews, FSceneTextures& SceneTextures, bool bIsEarlyDepthComplete,
	FNaniteBasePassVisibility& InNaniteBasePassVisibility,
	TArray<Nanite::FRasterResults, TInlineAllocator<2>>& NaniteRasterResults,
	TArray<Nanite::FPackedView, SceneRenderingAllocator>& PrimaryNaniteViews)
{
	LLM_SCOPE_BYTAG(Nanite);
	TRACE_CPUPROFILER_EVENT_SCOPE(InitNaniteRaster);

	NaniteRasterResults.AddDefaulted(InViews.Num());
	if (InNaniteBasePassVisibility.Query != nullptr)
	{
		// For now we'll share the same visibility results across all views
		for (int32 ViewIndex = 0; ViewIndex < NaniteRasterResults.Num(); ++ViewIndex)
		{
			NaniteRasterResults[ViewIndex].VisibilityQuery = InNaniteBasePassVisibility.Query;
		}

#if STATS
		// Launch a setup task that will process stats when the visibility task completes.
		GraphBuilder.AddSetupTask([Query = InNaniteBasePassVisibility.Query]
		{
			const FNaniteVisibilityResults* VisibilityResults = Nanite::GetVisibilityResults(Query);

			uint32 TotalRasterBins = 0;
			uint32 VisibleRasterBins = 0;
			VisibilityResults->GetRasterBinStats(VisibleRasterBins, TotalRasterBins);

			uint32 TotalShadingBins = 0;
			uint32 VisibleShadingBins = 0;
			VisibilityResults->GetShadingBinStats(VisibleShadingBins, TotalShadingBins);

			SET_DWORD_STAT(STAT_NaniteBasePassTotalRasterBins, TotalRasterBins);
			SET_DWORD_STAT(STAT_NaniteBasePassVisibleRasterBins, VisibleRasterBins);

			SET_DWORD_STAT(STAT_NaniteBasePassTotalShadingBins, TotalShadingBins);
			SET_DWORD_STAT(STAT_NaniteBasePassVisibleShadingBins, VisibleShadingBins);

		}, Nanite::GetVisibilityTask(InNaniteBasePassVisibility.Query));
#endif
	}

	const FIntPoint RasterTextureSize = SceneTextures.Depth.Target->Desc.Extent;

	// Primary raster view
	{
		Nanite::FSharedContext SharedContext{};
		SharedContext.FeatureLevel = Scene->GetFeatureLevel();
		SharedContext.ShaderMap = GetGlobalShaderMap(SharedContext.FeatureLevel);
		SharedContext.Pipeline = Nanite::EPipeline::Primary;

		FIntRect RasterTextureRect(0, 0, RasterTextureSize.X, RasterTextureSize.Y);
		if (InViews.Num() == 1)
		{
			const FViewInfo& View = InViews[0];
			if (View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0)
			{
				RasterTextureRect = View.ViewRect;
			}
		}

		Nanite::FRasterContext RasterContext;

		// Nanite::VisBuffer (Visibility Buffer Clear)
		{
			const FNaniteVisualizationData& VisualizationData = GetNaniteVisualizationData();

			bool bVisualizeActive = VisualizationData.IsActive() && ViewFamily.EngineShowFlags.VisualizeNanite;
			bool bVisualizeOverdraw = false;
			if (bVisualizeActive)
			{
				if (VisualizationData.GetActiveModeID() == 0) // Overview
				{
					bVisualizeOverdraw = VisualizationData.GetOverviewModeIDs().Contains(NANITE_VISUALIZE_OVERDRAW);
				}
				else
				{
					bVisualizeOverdraw = (VisualizationData.GetActiveModeID() == NANITE_VISUALIZE_OVERDRAW);
				}
			}

			RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteVisBuffer, "Nanite::VisBuffer");
			RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteVisBuffer);

			RasterContext = Nanite::InitRasterContext(
				GraphBuilder,
				SharedContext,
				ViewFamily,
				RasterTextureSize,
				RasterTextureRect,
				Nanite::EOutputBufferMode::VisBuffer,
				true, // bClearTarget
				true, // bAsyncCompute
				nullptr, 0, // Rect buffers
				nullptr, // ExternalDepthBuffer
				false, // bCustomPass
				bVisualizeActive,
				bVisualizeOverdraw
			);
		}

		Nanite::FConfiguration CullingConfig = { 0 };
		CullingConfig.bTwoPassOcclusion = true;
		CullingConfig.bUpdateStreaming = true;
		CullingConfig.bPrimaryContext = true;

		static FString EmptyFilterName = TEXT(""); // Empty filter represents primary view.
		CullingConfig.bExtractStats = Nanite::IsStatFilterActive(EmptyFilterName);

		const bool bDrawSceneViewsInOneNanitePass = InViews.Num() > 1 && Nanite::ShouldDrawSceneViewsInOneNanitePass(InViews[0]);

		// creates one or more Nanite views (normally one per view unless drawing multiple views together - e.g. Stereo ISR views)
		auto CreateNaniteViews = [bDrawSceneViewsInOneNanitePass, &InViews, &PrimaryNaniteViews, &GraphBuilder](const FViewInfo& View, int32 ViewIndex, const FIntPoint& RasterTextureSize, float MaxPixelsPerEdgeMultipler, TArray<FConvexVolume> &OutViewsCullingVolumes) -> Nanite::FPackedViewArray*
		{
			Nanite::FPackedViewArray::ArrayType OutViews;

			// always add the primary view. In case of bDrawSceneViewsInOneNanitePass HZB is built from all views so using viewrects
			// to account for a rare case when the primary view doesn't start from 0, 0 (maybe can happen in splitscreen?)
			FIntRect HZBTestRect = bDrawSceneViewsInOneNanitePass ?
				View.PrevViewInfo.ViewRect :
				FIntRect(0, 0, View.PrevViewInfo.ViewRect.Width(), View.PrevViewInfo.ViewRect.Height());

			Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(
				View,
				RasterTextureSize,
				NANITE_VIEW_FLAG_HZBTEST | NANITE_VIEW_FLAG_NEAR_CLIP,
				/* StreamingPriorityCategory = */ 3,
				/* MinBoundsRadius = */ 0.0f,
				MaxPixelsPerEdgeMultipler,
				&HZBTestRect
			);
			OutViewsCullingVolumes.Add(View.ViewFrustum);
			OutViews.Add(PackedView);
			PrimaryNaniteViews.Add(PackedView);

			if (bDrawSceneViewsInOneNanitePass)
			{
				// All other views in the family will need to be rendered in one go, to cover both ISR and (later) split-screen
				for (int32 ViewIdx = 1, NumViews = InViews.Num(); ViewIdx < NumViews; ++ViewIdx)
				{
					const FViewInfo& SecondaryViewInfo = InViews[ViewIdx];

					/* viewport rect in HZB space. For instanced stereo passes HZB is built for all atlased views */
					FIntRect SecondaryHZBTestRect = SecondaryViewInfo.PrevViewInfo.ViewRect;
					Nanite::FPackedView SecondaryPackedView = Nanite::CreatePackedViewFromViewInfo(
						SecondaryViewInfo,
						RasterTextureSize,
						NANITE_VIEW_FLAG_HZBTEST | NANITE_VIEW_FLAG_NEAR_CLIP,
						/* StreamingPriorityCategory = */ 3,
						/* MinBoundsRadius = */ 0.0f,
						MaxPixelsPerEdgeMultipler,
						&SecondaryHZBTestRect
					);
					OutViewsCullingVolumes.Add(SecondaryViewInfo.ViewFrustum);
					OutViews.Add(SecondaryPackedView);
					PrimaryNaniteViews.Add(SecondaryPackedView);
				}
			}

			return Nanite::FPackedViewArray::Create(GraphBuilder, OutViews.Num(), 1, MoveTemp(OutViews));
		};

		// in case of bDrawSceneViewsInOneNanitePass we only need one iteration
		uint32 ViewsToRender = (bDrawSceneViewsInOneNanitePass ? 1u : (uint32)InViews.Num());
		for (uint32 ViewIndex = 0; ViewIndex < ViewsToRender; ++ViewIndex)
		{
			Nanite::FRasterResults& RasterResults = NaniteRasterResults[ViewIndex];
			const FViewInfo& View = InViews[ViewIndex];
			// We don't check View.ShouldRenderView() since this is already taken care of by bDrawSceneViewsInOneNanitePass.
			// If bDrawSceneViewsInOneNanitePass is false, we need to render the secondary view even if ShouldRenderView() is false
			// NOTE: Except when there are no primitives to draw for the view
			if (View.bHasNoVisiblePrimitive)
			{
				continue;
			}
			
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, InViews.Num() > 1 && !bDrawSceneViewsInOneNanitePass, "View%u", ViewIndex);
			RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, InViews.Num() > 1 && bDrawSceneViewsInOneNanitePass, "View%u (together with %d more)", ViewIndex, InViews.Num() - 1);

			FIntRect ViewRect = bDrawSceneViewsInOneNanitePass ? FIntRect(0, 0, FamilySize.X, FamilySize.Y) : View.ViewRect;
			CullingConfig.SetViewFlags(View);

			float LODScaleFactor = 1.0f;
			if (View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale &&
				CVarNaniteViewMeshLODBiasEnable.GetValueOnRenderThread() != 0)
			{
				float TemporalUpscaleFactor = float(View.GetSecondaryViewRectSize().X) / float(ViewRect.Width());

				LODScaleFactor = TemporalUpscaleFactor * FMath::Exp2(-CVarNaniteViewMeshLODBiasOffset.GetValueOnRenderThread());
				LODScaleFactor = FMath::Min(LODScaleFactor, FMath::Exp2(-CVarNaniteViewMeshLODBiasMin.GetValueOnRenderThread()));
			}

			float MaxPixelsPerEdgeMultipler = 1.0f / LODScaleFactor;

			float QualityScale = Nanite::GStreamingManager.GetQualityScaleFactor();
			if (GDynamicNaniteScalingPrimary.GetSettings().IsEnabled())
			{
				QualityScale = FMath::Min(QualityScale, DynamicResolutionFractions[GDynamicNaniteScalingPrimary]);
			}
			MaxPixelsPerEdgeMultipler /= QualityScale;

			TArray<FConvexVolume> ViewsToRenderCullingVolumes;
			Nanite::FPackedViewArray* NaniteViewsToRender = CreateNaniteViews(View, ViewIndex, RasterTextureSize, MaxPixelsPerEdgeMultipler, ViewsToRenderCullingVolumes);

			TUniquePtr< Nanite::IRenderer > NaniteRenderer;

			// Nanite::VisBuffer (Culling and Rasterization)
			{
				DynamicRenderScaling::FRDGScope DynamicScalingScope(GraphBuilder, GDynamicNaniteScalingPrimary);

				RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteVisBuffer, "Nanite::VisBuffer");
				RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteVisBuffer);

				NaniteRenderer = Nanite::IRenderer::Create(
					GraphBuilder,
					*Scene,
					View,
					GetSceneUniforms(),
					SharedContext,
					RasterContext,
					CullingConfig,
					ViewRect,
					!bIsEarlyDepthComplete ? View.PrevViewInfo.NaniteHZB : View.PrevViewInfo.HZB
				);

				FSceneInstanceCullingQuery *SceneInstanceCullQuery = SceneCullingRenderer.CullInstances(GraphBuilder, ViewsToRenderCullingVolumes);
				NaniteRenderer->DrawGeometry(
					Scene->NaniteRasterPipelines[ENaniteMeshPass::BasePass],
					RasterResults.VisibilityQuery,
					*NaniteViewsToRender,
					SceneInstanceCullQuery
				);

				NaniteRenderer->ExtractResults( RasterResults );
			}

			// Nanite::BasePass (Depth Pre-Pass and HZB Build)
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteBasePass, "NaniteBasePass");
				RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteBasePass);

				// Emit velocity with depth if not writing it in base pass.
				FRDGTexture* VelocityBuffer = !IsUsingBasePassVelocity(ShaderPlatform) ? SceneTextures.Velocity : nullptr;

				Nanite::EmitDepthTargets(
					GraphBuilder,
					*Scene,
					InViews[ViewIndex],
					bDrawSceneViewsInOneNanitePass,
					RasterResults,
					SceneTextures.Depth.Target,
					VelocityBuffer
				);
				
				// Sanity check (always force Z prepass)
				check(bIsEarlyDepthComplete);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::Render(FRDGBuilder& GraphBuilder)
{
	if (!ViewFamily.EngineShowFlags.Rendering)
	{
		return;
	}

	// If this is scene capture rendering depth pre-pass, we'll take the shortcut function RenderSceneCaptureDepth if optimization switch is on.
	const ERendererOutput RendererOutput = GetRendererOutput();

	const bool bNaniteEnabled = ShouldRenderNanite();
	const bool bHasRayTracedOverlay = HasRayTracedOverlay(ViewFamily);

#if !UE_BUILD_SHIPPING
	RenderCaptureInterface::FScopedCapture RenderCapture(GCaptureNextDeferredShadingRendererFrame-- == 0, GraphBuilder, TEXT("DeferredShadingSceneRenderer"));
	// Prevent overflow every 2B frames.
	GCaptureNextDeferredShadingRendererFrame = FMath::Max(-1, GCaptureNextDeferredShadingRendererFrame);
#endif

	GPU_MESSAGE_SCOPE(GraphBuilder);

#if RHI_RAYTRACING
	if (RendererOutput == FSceneRenderer::ERendererOutput::FinalSceneColor)
	{
		GRayTracingGeometryManager->PreRender();

		// TODO: should only process build requests once per frame
		RHI_BREADCRUMB_EVENT_STAT(GraphBuilder.RHICmdList, RayTracingGeometry, "RayTracingGeometry");
		SCOPED_GPU_STAT(GraphBuilder.RHICmdList, RayTracingGeometry);

		GRayTracingGeometryManager->ProcessBuildRequests(GraphBuilder.RHICmdList);
	}
#endif

	FInitViewTaskDatas InitViewTaskDatas = OnRenderBegin(GraphBuilder);

	FRDGExternalAccessQueue ExternalAccessQueue;
	TUniquePtr<FVirtualTextureUpdater> VirtualTextureUpdater;
	FLumenSceneFrameTemporaries LumenFrameTemporaries(Views);

	FGPUSceneScopeBeginEndHelper GPUSceneScopeBeginEndHelper(GraphBuilder, Scene->GPUScene, GPUSceneDynamicContext);

	const bool bUseVirtualTexturing = UseVirtualTexturing(ShaderPlatform);

	// Virtual texturing runs for ERendererOutput::BasePass or ERendererOutput::FinalSceneColor
	if (bUseVirtualTexturing && RendererOutput != ERendererOutput::DepthPrepassOnly)
	{
		FVirtualTextureUpdateSettings Settings;
		Settings.EnableThrottling(!ViewFamily.bOverrideVirtualTextureThrottle);

		VirtualTextureUpdater = FVirtualTextureSystem::Get().BeginUpdate(GraphBuilder, FeatureLevel, Scene, Settings);
		VirtualTextureFeedbackBegin(GraphBuilder, Views, GetActiveSceneTexturesConfig().Extent);
	}

	// Compute & commit the final state of the entire dependency topology of the renderer.
	CommitFinalPipelineState();

	// Initialize global system textures (pass-through if already initialized).
	GSystemTextures.InitializeTextures(GraphBuilder.RHICmdList, FeatureLevel);

	UE::Tasks::TTask<void> UpdateLightFunctionAtlasTask;
	if (LightFunctionAtlas.IsLightFunctionAtlasEnabled())
	{
		UpdateLightFunctionAtlasTask = LaunchSceneRenderTask<void>(TEXT("UpdateLightFunctionAtlas"), [this]
			{
				UpdateLightFunctionAtlasTaskFunction();
			}, UE::Tasks::FTask());
	}

	{
		if (RendererOutput == ERendererOutput::FinalSceneColor)
		{
			// 1. Update sky atmosphere
			// This needs to be done prior to start Lumen scene lighting to ensure directional light color is correct, as the sun color needs atmosphere transmittance
			{
				const bool bPathTracedAtmosphere = ViewFamily.EngineShowFlags.PathTracing && Views.Num() > 0 && PathTracing::UsesReferenceAtmosphere(Views[0]);
				if (ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags) && !bPathTracedAtmosphere)
				{
					for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
					{
						if (Scene->AtmosphereLights[LightIndex])
						{
							PrepareSunLightProxy(*Scene->GetSkyAtmosphereSceneInfo(),LightIndex, *Scene->AtmosphereLights[LightIndex]);
						}
					}
				}
				else
				{
					Scene->ResetAtmosphereLightsProperties();
				}
			}

			// 2. Update lumen scene
			{
				InitViewTaskDatas.LumenFrameTemporaries = &LumenFrameTemporaries;
	
				// Important that this uses consistent logic throughout the frame, so evaluate once and pass in the flag from here
				// NOTE: Must be done after  system texture initialization
				// TODO: This doesn't take into account the potential for split screen views with separate shadow caches
				const bool bEnableVirtualShadowMaps = UseVirtualShadowMaps(ShaderPlatform, FeatureLevel) && ViewFamily.EngineShowFlags.DynamicShadows && !bHasRayTracedOverlay;
				VirtualShadowMapArray.Initialize(GraphBuilder, Scene->GetVirtualShadowMapCache(), bEnableVirtualShadowMaps, ViewFamily.EngineShowFlags);
	
				if (InitViewTaskDatas.LumenFrameTemporaries)
				{
					BeginUpdateLumenSceneTasks(GraphBuilder, *InitViewTaskDatas.LumenFrameTemporaries);
				}
	
				BeginGatherLumenLights(*InitViewTaskDatas.LumenFrameTemporaries, InitViewTaskDatas.LumenDirectLighting, InitViewTaskDatas.VisibilityTaskData, UpdateLightFunctionAtlasTask);
			}
		}

		if (bNaniteEnabled)
		{
			TArray<FConvexVolume, TInlineAllocator<2>> NaniteCullingViews;

			// For now we'll share the same visibility results across all views
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];
				NaniteCullingViews.Add(View.ViewFrustum);
			}

			FNaniteVisibility& NaniteVisibility = Scene->NaniteVisibility[ENaniteMeshPass::BasePass];
			const FNaniteRasterPipelines&  NaniteRasterPipelines  = Scene->NaniteRasterPipelines[ENaniteMeshPass::BasePass];
			const FNaniteShadingPipelines& NaniteShadingPipelines = Scene->NaniteShadingPipelines[ENaniteMeshPass::BasePass];

			NaniteVisibility.BeginVisibilityFrame();

			NaniteBasePassVisibility.Visibility = &NaniteVisibility;
			NaniteBasePassVisibility.Query = NaniteVisibility.BeginVisibilityQuery(
				Allocator,
				*Scene,
				NaniteCullingViews,
				&NaniteRasterPipelines,
				&NaniteShadingPipelines,
				InitViewTaskDatas.VisibilityTaskData->GetComputeRelevanceTask()
			);
		}
	}
	ShaderPrint::BeginViews(GraphBuilder, Views);

	ON_SCOPE_EXIT
	{
		ShaderPrint::EndViews(Views);
	};

	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
		PrepareDistanceFieldScene(GraphBuilder, ExternalAccessQueue);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			ShadingEnergyConservation::Init(GraphBuilder, View);

			FGlintShadingLUTsStateData::Init(GraphBuilder, View);
		}

		// kick off dependent scene updates 
		ShadowSceneRenderer->BeginRender(GraphBuilder);

#if RHI_RAYTRACING
		// Initialize ray tracing flags, in case they weren't initialized in the CreateSceneRenderers code path
		InitializeRayTracingFlags_RenderThread();

		if (bAnyRayTracingPassEnabled)
		{
			const int32 ReferenceViewIndex = 0;
			FViewInfo& ReferenceView = Views[ReferenceViewIndex];

			InitViewTaskDatas.RayTracingGatherInstances = RayTracing::CreateGatherInstancesTaskData(
				Allocator,
				*Scene,
				ReferenceView,
				ViewFamily,
				GetViewPipelineState(ReferenceView).DiffuseIndirectMethod,
				GetViewPipelineState(ReferenceView).ReflectionsMethod);

			RayTracing::BeginGatherInstances(*InitViewTaskDatas.RayTracingGatherInstances, InitViewTaskDatas.VisibilityTaskData->GetFrustumCullTask());
		}
#endif
	}

	UE::SVT::GetStreamingManager().BeginAsyncUpdate(GraphBuilder);

	bool bUpdateNaniteStreaming = false;
	bool bVisualizeNanite = false;
	if (bNaniteEnabled)
	{
		Nanite::GGlobalResources.Update(GraphBuilder);

		// Only update Nanite streaming residency for the first view when multiple view rendering (nDisplay) is enabled.
		// Streaming requests are still accumulated from the remaining views.
		bUpdateNaniteStreaming =  !ViewFamily.bIsMultipleViewFamily || ViewFamily.bIsFirstViewInMultipleViewFamily;
		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.BeginAsyncUpdate(GraphBuilder);
		}

		FNaniteVisualizationData& NaniteVisualization = GetNaniteVisualizationData();
		if (Views.Num() > 0)
		{
			const FName& NaniteViewMode = Views[0].CurrentNaniteVisualizationMode;
			if (NaniteVisualization.Update(NaniteViewMode))
			{
				// When activating the view modes from the command line, automatically enable the VisualizeNanite show flag for convenience.
				ViewFamily.EngineShowFlags.SetVisualizeNanite(true);
			}

			bVisualizeNanite = NaniteVisualization.IsActive() && ViewFamily.EngineShowFlags.VisualizeNanite;
		}
	}

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RenderOther);

	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_Render, FColor::Emerald);

#if WITH_MGPU
	ComputeGPUMasks(&GraphBuilder.RHICmdList);
#endif // WITH_MGPU

	// By default, limit our GPU usage to only GPUs specified in the view masks.
	RDG_GPU_MASK_SCOPE(GraphBuilder, ViewFamily.EngineShowFlags.PathTracing ? FRHIGPUMask::All() : AllViewsGPUMask);
	RDG_EVENT_SCOPE(GraphBuilder, "Scene");
	FString FrameNumDescription = FString::Printf(TEXT("%s Frame: %d"), *ViewFamily.ProfileDescription, GFrameCounterRenderThread);
	RDG_GPU_STAT_SCOPE_VERBOSE(GraphBuilder, Unaccounted, *FrameNumDescription);
	
	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Render_Init);
		RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, AllocateRendertargets);

		// Force the subsurface profile texture to be updated.
		SubsurfaceProfile::UpdateSubsurfaceProfileTexture(GraphBuilder, ShaderPlatform);
		SpecularProfile::UpdateSpecularProfileTextureAtlas(GraphBuilder, ShaderPlatform);

		// Force the rect light texture & IES texture to be updated.
		RectLightAtlas::UpdateAtlasTexture(GraphBuilder, FeatureLevel);
		IESAtlas::UpdateAtlasTexture(GraphBuilder, ShaderPlatform);
	}

	FSceneTexturesConfig& SceneTexturesConfig = GetActiveSceneTexturesConfig();
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Create(GraphBuilder);

	const bool bAllowStaticLighting = !bHasRayTracedOverlay && IsStaticLightingAllowed();

	// if DDM_AllOpaqueNoVelocity was used, then velocity should have already been rendered as well
	const bool bIsEarlyDepthComplete = (DepthPass.EarlyZPassMode == DDM_AllOpaque || DepthPass.EarlyZPassMode == DDM_AllOpaqueNoVelocity);

	// Use read-only depth in the base pass if we have a full depth prepass.
	const bool bAllowReadOnlyDepthBasePass = bIsEarlyDepthComplete
		&& !ViewFamily.EngineShowFlags.ShaderComplexity
		&& !ViewFamily.UseDebugViewPS()
		&& !ViewFamily.EngineShowFlags.Wireframe
		&& !ViewFamily.EngineShowFlags.LightMapDensity;

	const FExclusiveDepthStencil::Type BasePassDepthStencilAccess =
		bAllowReadOnlyDepthBasePass
		? FExclusiveDepthStencil::DepthRead_StencilWrite
		: FExclusiveDepthStencil::DepthWrite_StencilWrite;

	// Find the visible primitives.
	FInstanceCullingManager& InstanceCullingManager = *GraphBuilder.AllocObject<FInstanceCullingManager>(GetSceneUniforms(), Scene->GPUScene.IsEnabled(), GraphBuilder);

	::Substrate::PreInitViews(*Scene);

	FSceneTextures::InitializeViewFamily(GraphBuilder, ViewFamily, FamilySize);
	FSceneTextures& SceneTextures = GetActiveSceneTextures();

	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, VisibilityCommands, "VisibilityCommands");
		RDG_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
		BeginInitViews(GraphBuilder, SceneTexturesConfig, InstanceCullingManager, ExternalAccessQueue, InitViewTaskDatas);
	}

#if !UE_BUILD_SHIPPING
	if (CVarStallInitViews.GetValueOnRenderThread() > 0.0f)
	{
		SCOPE_CYCLE_COUNTER(STAT_InitViews_Intentional_Stall);
		FPlatformProcess::Sleep(CVarStallInitViews.GetValueOnRenderThread() / 1000.0f);
	}
#endif

	extern TSet<IPersistentViewUniformBufferExtension*> PersistentViewUniformBufferExtensions;

	for (IPersistentViewUniformBufferExtension* Extension : PersistentViewUniformBufferExtensions)
	{
		Extension->BeginFrame();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			// Must happen before RHI thread flush so any tasks we dispatch here can land in the idle gap during the flush
			Extension->PrepareView(&Views[ViewIndex]);
		}
	}

#if RHI_RAYTRACING
	const int32 ReferenceViewIndex = 0;
	FViewInfo& ReferenceView = Views[ReferenceViewIndex];
	FRayTracingScene& RayTracingScene = Scene->RayTracingScene;
	FRayTracingShaderBindingTable& RayTracingSBT = Scene->RayTracingSBT;
#endif

	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
#if RHI_RAYTRACING
		// Prepare the scene for rendering this frame.
		RayTracingScene.Reset(IsRayTracingInstanceDebugDataEnabled(ReferenceView)); // Resets the internal arrays, but does not release any resources.

		if (ShouldPrepareRayTracingDecals(*Scene, ViewFamily))
		{
			// Calculate decal grid for ray tracing per view since decal fade is view dependent
			// TODO: investigate reusing the same grid for all views (ie: different callable shader SBT entries for each view so fade alpha is still correct for each view)

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];
				View.RayTracingDecalUniformBuffer = CreateRayTracingDecalData(GraphBuilder, *Scene, View, RayTracingScene.NumCallableShaderSlots);
				View.bHasRayTracingDecals = true;
				RayTracingScene.NumCallableShaderSlots += Scene->Decals.Num();
			}
		}
		else
		{
			TRDGUniformBufferRef<FRayTracingDecals> NullRayTracingDecalUniformBuffer = CreateNullRayTracingDecalsUniformBuffer(GraphBuilder);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				FViewInfo& View = Views[ViewIndex];
				View.RayTracingDecalUniformBuffer = NullRayTracingDecalUniformBuffer;
				View.bHasRayTracingDecals = false;
			}
		}

		if (ViewFamily.EngineShowFlags.PathTracing)
		{
			// If we might be path tracing the clouds -- call the path tracer's method for cloud callable shader setup
			// this will skip work if cloud rendering is not being used
			PreparePathTracingCloudMaterial(Scene, Views);
		}

		if (IsRayTracingEnabled(ViewFamily.GetShaderPlatform()) && GRHISupportsRayTracingShaders)
		{
			// Nanite raytracing manager update must run before GPUScene update since it can modify primitive data
			Nanite::GRayTracingManager.Update();

			if (!ViewFamily.EngineShowFlags.PathTracing)
			{
				// get the default lighting miss shader (to implicitly fill in the MissShader library before the RT pipeline is created)
				GetRayTracingLightingMissShader(ReferenceView.ShaderMap);
				RayTracingScene.NumMissShaderSlots++;
			}

			if (ViewFamily.EngineShowFlags.LightFunctions)
			{
				// gather all the light functions that may be used (and also count how many miss shaders we will need)
				FRayTracingLightFunctionMap RayTracingLightFunctionMap;
				if (ViewFamily.EngineShowFlags.PathTracing)
				{
					RayTracingLightFunctionMap = GatherLightFunctionLightsPathTracing(Scene, ViewFamily.EngineShowFlags, ReferenceView.GetFeatureLevel());
				}
				else
				{
					RayTracingLightFunctionMap = GatherLightFunctionLights(Scene, ViewFamily.EngineShowFlags, ReferenceView.GetFeatureLevel());
				}
				if (!RayTracingLightFunctionMap.IsEmpty())
				{
					// If we got some light functions in our map, store them in the RDG blackboard so downstream functions can use them.
					// The map itself will be strictly read-only from this point on.
					GraphBuilder.Blackboard.Create<FRayTracingLightFunctionMap>(MoveTemp(RayTracingLightFunctionMap));
				}
			}
		}
#endif // RHI_RAYTRACING

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		Scene->DebugRender(Views);
#endif
	}

	InitViewTaskDatas.VisibilityTaskData->FinishGatherDynamicMeshElements(BasePassDepthStencilAccess, InstanceCullingManager, VirtualTextureUpdater.Get());

	// Notify the FX system that the scene is about to be rendered.
	// TODO: These should probably be moved to scene extensions
	if (FXSystem && Views.IsValidIndex(0))
	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_FXSystem_PreRender);
		FXSystem->PreRender(GraphBuilder, GetSceneViews(), GetSceneUniforms(), bIsFirstSceneRenderer /*bAllowGPUParticleUpdate*/);
		if (FGPUSortManager* GPUSortManager = FXSystem->GetGPUSortManager())
		{
			GPUSortManager->OnPreRender(GraphBuilder);
		}
	}

	{
		RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, UpdateGPUScene);
		RDG_EVENT_SCOPE_STAT(GraphBuilder, GPUSceneUpdate, "GPUSceneUpdate");
		RDG_GPU_STAT_SCOPE(GraphBuilder, GPUSceneUpdate);

		if (bIsFirstSceneRenderer)
		{
			GraphBuilder.SetFlushResourcesRHI();
		}

		for (int32 ViewIndex = 0; ViewIndex < AllViews.Num(); ViewIndex++)
		{
			FViewInfo& View = *AllViews[ViewIndex];
			RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

			Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView(GraphBuilder, View);

			Scene->GPUScene.DebugRender(GraphBuilder, GetSceneUniforms(), View);
		}

		SceneCullingRenderer.DebugRender(GraphBuilder, Views);

		InstanceCullingManager.BeginDeferredCulling(GraphBuilder, Scene->GPUScene);

		if (Views.Num() > 0)
		{
			FViewInfo& View = Views[0];
			Scene->UpdatePhysicsField(GraphBuilder, View);
		}
	}

	// Allow scene extensions to affect the scene uniform buffer after GPU scene has fully updated
	GetSceneExtensionsRenderers().UpdateSceneUniformBuffer(GraphBuilder, GetSceneUniforms());

	const bool bUseGBuffer = IsUsingGBuffers(ShaderPlatform);
	const bool bShouldRenderVolumetricFog = ShouldRenderVolumetricFog();
	const bool bShouldRenderLocalFogVolume = ShouldRenderLocalFogVolume(Scene, ViewFamily);
	const bool bShouldRenderLocalFogVolumeDuringHeightFogPass = ShouldRenderLocalFogVolumeDuringHeightFogPass(Scene, ViewFamily);
	const bool bShouldRenderLocalFogVolumeInVolumetricFog = ShouldRenderLocalFogVolumeInVolumetricFog(Scene, ViewFamily, bShouldRenderLocalFogVolume);

	const bool bRenderDeferredLighting = ViewFamily.EngineShowFlags.Lighting
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& ViewFamily.EngineShowFlags.DeferredLighting
		&& bUseGBuffer
		&& !bHasRayTracedOverlay;

	bool bComputeLightGrid = false;
	bool bAnyLumenEnabled = false;

	// Virtual texturing runs for ERendererOutput::BasePass or ERendererOutput::FinalSceneColor
	if (RendererOutput != ERendererOutput::DepthPrepassOnly)
	{
		if (bUseVirtualTexturing)
		{
			// Note, should happen after the GPU-Scene update to ensure rendering to runtime virtual textures is using the correctly updated scene
			FVirtualTextureSystem::Get().EndUpdate(GraphBuilder, MoveTemp(VirtualTextureUpdater), FeatureLevel);
		}
	}

	UE::Tasks::TTask<FSortedLightSetSceneInfo*> GatherAndSortLightsTask;

	if (RendererOutput == ERendererOutput::FinalSceneColor)
	{
#if RHI_RAYTRACING
		if (bAnyRayTracingPassEnabled)
		{
			RayTracing::FinishGatherInstances(
				GraphBuilder,
				*InitViewTaskDatas.RayTracingGatherInstances,
				RayTracingScene,
				RayTracingSBT,
				DynamicReadBufferForRayTracing,
				Allocator);
		}
#endif // RHI_RAYTRACING

		{
			if (bUseGBuffer)
			{
				bComputeLightGrid = bRenderDeferredLighting;
			}
			else
			{
				bComputeLightGrid = ViewFamily.EngineShowFlags.Lighting;
			}

			if (!bHasRayTracedOverlay)
			{
				for (const FViewInfo& View : Views)
				{
					bAnyLumenEnabled = bAnyLumenEnabled
						|| GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen
						|| GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen;
				}
			}

			bComputeLightGrid |= (
				bShouldRenderVolumetricFog ||
				VolumetricCloudWantsToSampleLocalLights(Scene, ViewFamily.EngineShowFlags) ||
				ViewFamily.ViewMode != VMI_Lit ||
				bAnyLumenEnabled ||
				VirtualShadowMapArray.IsEnabled() ||
				ShouldVisualizeLightGrid() ||
				ShouldRenderLocalFogVolume(Scene, ViewFamily)); // Needed when accessing forward light data for the directional light
			bComputeLightGrid &= !ViewFamily.EngineShowFlags.PathTracing;
		}

		{
			extern bool IsVSMOnePassProjectionEnabled(const FEngineShowFlags& ShowFlags);
			extern UE::Tasks::FTask GetGatherAndSortLightsPrerequisiteTask(const FDynamicShadowsTaskData* TaskData);

			auto* SortedLightSet = GraphBuilder.AllocObject<FSortedLightSetSceneInfo>();
			const bool bShadowedLightsInClustered = ShouldUseClusteredDeferredShading()
				&& IsVSMOnePassProjectionEnabled(ViewFamily.EngineShowFlags)
				&& VirtualShadowMapArray.IsEnabled();

			TArray<UE::Tasks::FTask, TInlineAllocator<2>> IssuedTasksCompletionEvents;
			IssuedTasksCompletionEvents.Add(GetGatherAndSortLightsPrerequisiteTask(InitViewTaskDatas.DynamicShadows));
			IssuedTasksCompletionEvents.Add(UpdateLightFunctionAtlasTask);

			GatherAndSortLightsTask = LaunchSceneRenderTask<FSortedLightSetSceneInfo*>(UE_SOURCE_LOCATION, [this, SortedLightSet, bShadowedLightsInClustered]
			{
				GatherAndSortLights(*SortedLightSet, bShadowedLightsInClustered);
				return SortedLightSet;
			}, IssuedTasksCompletionEvents);
		}
	}

	// force using occ queries for wireframe if rendering is parented or frozen in the first view
	check(Views.Num());
	#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const bool bIsViewFrozen = false;
	#else
		const bool bIsViewFrozen = Views[0].State && ((FSceneViewState*)Views[0].State)->bIsFrozen;
	#endif

	
	const bool bIsOcclusionTesting = DoOcclusionQueries()
		&& (!ViewFamily.EngineShowFlags.Wireframe || bIsViewFrozen);
	const bool bNeedsPrePass = ShouldRenderPrePass();

	// Sanity check - Note: Nanite forces a Z prepass in ShouldForceFullDepthPass()
	check(!UseNanite(ShaderPlatform) || bNeedsPrePass);

	GetSceneExtensionsRenderers().PreRender(GraphBuilder);
	GEngine->GetPreRenderDelegateEx().Broadcast(GraphBuilder);

	if (DepthPass.IsComputeStencilDitherEnabled())
	{
		AddDitheredStencilFillPass(GraphBuilder, Views, SceneTextures.Depth.Target, DepthPass);
	}

	if (bNaniteEnabled)
	{
		// Must happen before any Nanite rendering in the frame
		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.EndAsyncUpdate(GraphBuilder);

			const TMap<uint32, uint32> ModifiedResources = Nanite::GStreamingManager.GetAndClearModifiedResources();
#if RHI_RAYTRACING
			if (RendererOutput == ERendererOutput::FinalSceneColor)
			{
				Nanite::GRayTracingManager.RequestUpdates(ModifiedResources);
			}
#endif
		}
	}

	{
		RDG_RHI_GPU_STAT_SCOPE(GraphBuilder, VisibilityCommands);
		EndInitViews(GraphBuilder, LumenFrameTemporaries, InstanceCullingManager, InitViewTaskDatas);
	}

	// Substrate initialisation is always run even when not enabled.
	// Need to run after EndInitViews() to ensure ViewRelevance computation are completed
	const bool bSubstrateEnabled = Substrate::IsSubstrateEnabled();
	Substrate::InitialiseSubstrateFrameSceneData(GraphBuilder, *this);

	UE::SVT::GetStreamingManager().EndAsyncUpdate(GraphBuilder);

	FHairStrandsBookmarkParameters& HairStrandsBookmarkParameters = *GraphBuilder.AllocObject<FHairStrandsBookmarkParameters>();
	if (IsHairStrandsEnabled(EHairStrandsShaderType::All, Scene->GetShaderPlatform()) && RendererOutput == ERendererOutput::FinalSceneColor)
	{
		CreateHairStrandsBookmarkParameters(Scene, Views, AllFamilyViews, HairStrandsBookmarkParameters);
		check(Scene->HairStrandsSceneData.TransientResources);
		HairStrandsBookmarkParameters.TransientResources = Scene->HairStrandsSceneData.TransientResources;
		RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessTasks, HairStrandsBookmarkParameters);

		// Interpolation needs to happen after the skin cache run as there is a dependency 
		// on the skin cache output.
		const bool bRunHairStrands = HairStrandsBookmarkParameters.HasInstances() && (Views.Num() > 0);
		if (bRunHairStrands)
		{
			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessCardsAndMeshesInterpolation_PrimaryView, HairStrandsBookmarkParameters);
		}
		else
		{
			for (FViewInfo& View : Views)
			{
				View.HairStrandsViewData.UniformBuffer = HairStrands::CreateDefaultHairStrandsViewUniformBuffer(GraphBuilder, View);
			}
		}
	}

	ExternalAccessQueue.Submit(GraphBuilder);

	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags);
	const ESkyAtmospherePassLocation SkyAtmospherePassLocation = GetSkyAtmospherePassLocation();
	FSkyAtmospherePendingRDGResources SkyAtmospherePendingRDGResources;
	if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforePrePass && bShouldRenderSkyAtmosphere)
	{
		// Generate the Sky/Atmosphere look up tables overlaping the pre-pass
		RenderSkyAtmosphereLookUpTables(GraphBuilder, /* out */ SkyAtmospherePendingRDGResources);
	}

	RenderWaterInfoTexture(GraphBuilder, *this, Scene);

	const bool bShouldRenderVelocities = ShouldRenderVelocities();
	const EShaderPlatform Platform = GetViewFamilyInfo(Views).GetShaderPlatform();
	const bool bBasePassCanOutputVelocity = FVelocityRendering::BasePassCanOutputVelocity(Platform);
	const bool bHairStrandsEnable = HairStrandsBookmarkParameters.HasInstances() && Views.Num() > 0 && IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform);
	const bool bForceVelocityOutput = bHairStrandsEnable || ShouldRenderDistortion();

	auto RenderPrepassAndVelocity = [&](auto& InViews, auto& InNaniteBasePassVisibility, auto& NaniteRasterResults, auto& PrimaryNaniteViews)
	{
		FRDGTextureRef FirstStageDepthBuffer = nullptr;
		{
			// Both compute approaches run earlier, so skip clearing stencil here, just load existing.
			const ERenderTargetLoadAction StencilLoadAction = DepthPass.IsComputeStencilDitherEnabled()
				? ERenderTargetLoadAction::ELoad
				: ERenderTargetLoadAction::EClear;

			const ERenderTargetLoadAction DepthLoadAction = ERenderTargetLoadAction::EClear;
			AddClearDepthStencilPass(GraphBuilder, SceneTextures.Depth.Target, DepthLoadAction, StencilLoadAction);

			// Draw the scene pre-pass / early z pass, populating the scene depth buffer and HiZ
			if (bNeedsPrePass)
			{
				RenderPrePass(GraphBuilder, InViews, SceneTextures.Depth.Target, InstanceCullingManager, &FirstStageDepthBuffer);
			}
			else
			{
				// We didn't do the prepass, but we still want the HMD mask if there is one
				RenderPrePassHMD(GraphBuilder, InViews, SceneTextures.Depth.Target);
			}

			// special pass for DDM_AllOpaqueNoVelocity, which uses the velocity pass to finish the early depth pass write
			if (bShouldRenderVelocities && Scene->EarlyZPassMode == DDM_AllOpaqueNoVelocity && RendererOutput == ERendererOutput::FinalSceneColor)
			{
				// Render the velocities of movable objects
				RenderVelocities(GraphBuilder, InViews, SceneTextures, EVelocityPass::Opaque, bForceVelocityOutput);
			}
		}

		{
			Scene->WaitForCacheNaniteMaterialBinsTask();

			if (bNaniteEnabled && InViews.Num() > 0)
			{
				RenderNanite(GraphBuilder, InViews, SceneTextures, bIsEarlyDepthComplete, InNaniteBasePassVisibility, NaniteRasterResults, PrimaryNaniteViews);
			}
		}

		if (FirstStageDepthBuffer)
		{
			SceneTextures.PartialDepth = FirstStageDepthBuffer;
			AddResolveSceneDepthPass(GraphBuilder, InViews, SceneTextures.PartialDepth);
		}
		else
		{
			// Setup default partial depth to be scene depth so that it also works on transparent emitter when partial depth has not been generated.
			SceneTextures.PartialDepth = SceneTextures.Depth;
		}
		SceneTextures.SetupMode = ESceneTextureSetupMode::SceneDepth;
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		AddResolveSceneDepthPass(GraphBuilder, InViews, SceneTextures.Depth);
	};

	FDBufferTextures DBufferTextures = CreateDBufferTextures(GraphBuilder, SceneTextures.Config.Extent, ShaderPlatform);

	// Initialise local fog volume with dummy data before volumetric cloud view initialization (further down) which can bind LFV data.
	// Also need to do this before custom render passes (included in AllViews), as base pass rendering may bind LFV data.
	SetDummyLocalFogVolumeForViews(GraphBuilder, AllViews);

	if (CustomRenderPassInfos.Num() > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_CustomRenderPasses);
		RDG_EVENT_SCOPE_STAT(GraphBuilder, CustomRenderPasses, "CustomRenderPasses");
		RDG_GPU_STAT_SCOPE(GraphBuilder, CustomRenderPasses);

		// We want to reset the scene texture uniform buffer to its original state after custom render passes,
		// so they can't affect downstream rendering.
		ESceneTextureSetupMode OriginalSceneTextureSetupMode = SceneTextures.SetupMode;
		TRDGUniformBufferRef<FSceneTextureUniformParameters> OriginalSceneTextureUniformBuffer = SceneTextures.UniformBuffer;

		for (int32 i = 0; i < CustomRenderPassInfos.Num(); ++i)
		{
			FCustomRenderPassBase* CustomRenderPass = CustomRenderPassInfos[i].CustomRenderPass;
			TArray<FViewInfo>& CustomRenderPassViews = CustomRenderPassInfos[i].Views;
			FNaniteShadingCommands& NaniteBasePassShadingCommands = CustomRenderPassInfos[i].NaniteBasePassShadingCommands;
			check(CustomRenderPass);

			CustomRenderPass->BeginPass(GraphBuilder);

			{
				QUICK_SCOPE_CYCLE_COUNTER(STAT_CustomRenderPass);
				RDG_EVENT_SCOPE(GraphBuilder, "CustomRenderPass[%d] %s", i, *CustomRenderPass->GetDebugName());

				CustomRenderPass->PreRender(GraphBuilder);

				TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
				TArray<Nanite::FPackedView, SceneRenderingAllocator> PrimaryNaniteViews;
				FNaniteBasePassVisibility DummyNaniteBasePassVisibility;
				RenderPrepassAndVelocity(CustomRenderPassViews, DummyNaniteBasePassVisibility, NaniteRasterResults, PrimaryNaniteViews);

				const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult = nullptr;
				if (ShouldRenderSingleLayerWaterDepthPrepass(CustomRenderPassViews))
				{
					SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, CustomRenderPassViews, SceneTextures);
				}

				const FSceneCaptureCustomRenderPassUserData& SceneCaptureUserData = FSceneCaptureCustomRenderPassUserData::Get(CustomRenderPass);

				if (CustomRenderPass->GetRenderMode() == FCustomRenderPassBase::ERenderMode::DepthAndBasePass)
				{
					SceneTextures.SetupMode |= ESceneTextureSetupMode::SceneColor;
					SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

					if (bNaniteEnabled)
					{
						Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::BasePass, NaniteBasePassShadingCommands, Nanite::EBuildShadingCommandsMode::Custom);
					}

					RenderBasePass(*this, GraphBuilder, CustomRenderPassViews, SceneTextures, DBufferTextures, BasePassDepthStencilAccess, /*ForwardScreenSpaceShadowMaskTexture=*/nullptr, InstanceCullingManager, bNaniteEnabled, NaniteBasePassShadingCommands, NaniteRasterResults);

					if (ShouldRenderSingleLayerWater(CustomRenderPassViews))
					{
						// GBuffer code paths in RenderSingleLayerWater don't use the bIsCameraUnderWater flag, so just pass in false.  Normally this is
						// computed by a render extension, but those aren't run for custom render passes.
						FSceneWithoutWaterTextures SceneWithoutWaterTextures;
						RenderSingleLayerWater(GraphBuilder, CustomRenderPassViews, SceneTextures, SingleLayerWaterPrePassResult, /*bShouldRenderVolumetricCloud=*/false, SceneWithoutWaterTextures, LumenFrameTemporaries, /*bIsCameraUnderWater=*/false);
					}

					FCustomRenderPassBase::ERenderOutput RenderOutput = CustomRenderPass->GetRenderOutput();
					if (RenderOutput == FCustomRenderPassBase::ERenderOutput::BaseColor || RenderOutput == FCustomRenderPassBase::ERenderOutput::Normal ||
						!SceneCaptureUserData.UserSceneTextureBaseColor.IsNone() || !SceneCaptureUserData.UserSceneTextureNormal.IsNone() || !SceneCaptureUserData.UserSceneTextureSceneColor.IsNone())
					{
						// CopySceneCaptureComponentToTarget uses scene texture uniforms
						SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;
						SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
					}
				}

				CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, CustomRenderPass->GetRenderTargetTexture(), ViewFamily, CustomRenderPassViews);

				if (!SceneCaptureUserData.UserSceneTextureBaseColor.IsNone())
				{
					bool bFirstRender;
					FRDGTextureRef BaseColorSceneTexture = SceneTextures.FindOrAddUserSceneTexture(GraphBuilder, 0, SceneCaptureUserData.UserSceneTextureBaseColor, SceneCaptureUserData.SceneTextureDivisor, bFirstRender, nullptr, CustomRenderPassViews[0].ViewRect);
#if !(UE_BUILD_SHIPPING)
					SceneTextures.UserSceneTextureEvents.Add({ EUserSceneTextureEvent::CustomRenderPass, NAME_None, (uint16)FCustomRenderPassBase::ERenderOutput::BaseColor, 0, (const UMaterialInterface*)CustomRenderPass });
#endif

					CustomRenderPass->OverrideRenderOutput(FCustomRenderPassBase::ERenderOutput::BaseColor);
					CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, BaseColorSceneTexture, ViewFamily, CustomRenderPassViews);
				}

				if (!SceneCaptureUserData.UserSceneTextureNormal.IsNone())
				{
					bool bFirstRender;
					FRDGTextureRef NormalSceneTexture = SceneTextures.FindOrAddUserSceneTexture(GraphBuilder, 0, SceneCaptureUserData.UserSceneTextureNormal, SceneCaptureUserData.SceneTextureDivisor, bFirstRender, nullptr, CustomRenderPassViews[0].ViewRect);
#if !(UE_BUILD_SHIPPING)
					SceneTextures.UserSceneTextureEvents.Add({ EUserSceneTextureEvent::CustomRenderPass, NAME_None, (uint16)FCustomRenderPassBase::ERenderOutput::Normal, 0, (const UMaterialInterface*)CustomRenderPass });
#endif

					CustomRenderPass->OverrideRenderOutput(FCustomRenderPassBase::ERenderOutput::Normal);
					CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, NormalSceneTexture, ViewFamily, CustomRenderPassViews);
				}

				if (!SceneCaptureUserData.UserSceneTextureSceneColor.IsNone())
				{
					bool bFirstRender;
					FRDGTextureRef SceneColorSceneTexture = SceneTextures.FindOrAddUserSceneTexture(GraphBuilder, 0, SceneCaptureUserData.UserSceneTextureSceneColor, SceneCaptureUserData.SceneTextureDivisor, bFirstRender, nullptr, CustomRenderPassViews[0].ViewRect);
#if !(UE_BUILD_SHIPPING)
					SceneTextures.UserSceneTextureEvents.Add({ EUserSceneTextureEvent::CustomRenderPass, NAME_None, (uint16)FCustomRenderPassBase::ERenderOutput::SceneColorAndAlpha, 0, (const UMaterialInterface*)CustomRenderPass });
#endif

					CustomRenderPass->OverrideRenderOutput(FCustomRenderPassBase::ERenderOutput::SceneColorAndAlpha);
					CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, SceneColorSceneTexture, ViewFamily, CustomRenderPassViews);
				}

				CustomRenderPass->PostRender(GraphBuilder);

				// Mips are normally generated in UpdateSceneCaptureContentDeferred_RenderThread, but that doesn't run when the
				// scene capture runs as a custom render pass.  The function does nothing if the render target doesn't have mips.
				if (CustomRenderPassViews[0].bIsSceneCapture)
				{
					FGenerateMips::Execute(GraphBuilder, FeatureLevel, CustomRenderPass->GetRenderTargetTexture(), FGenerateMipsParams());
				}

			#if WITH_MGPU
				DoCrossGPUTransfers(GraphBuilder, CustomRenderPass->GetRenderTargetTexture(), CustomRenderPassViews, false, FRHIGPUMask::All(), nullptr);
			#endif
			}

			CustomRenderPass->EndPass(GraphBuilder);

			// Restore original scene texture uniforms
			SceneTextures.SetupMode = OriginalSceneTextureSetupMode;
			SceneTextures.UniformBuffer = OriginalSceneTextureUniformBuffer;
		}
	}

	TArray<Nanite::FRasterResults, TInlineAllocator<2>> NaniteRasterResults;
	TArray<Nanite::FPackedView, SceneRenderingAllocator> PrimaryNaniteViews;
	RenderPrepassAndVelocity(Views, NaniteBasePassVisibility, NaniteRasterResults, PrimaryNaniteViews);

	// Run Nanite compute commands early in the frame to allow some task overlap on the CPU until the base pass runs.
	if (bNaniteEnabled && RendererOutput != ERendererOutput::DepthPrepassOnly && !bHasRayTracedOverlay)
	{
		Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::BasePass, Scene->NaniteShadingCommands[ENaniteMeshPass::BasePass]);
		if (bAnyLumenEnabled && RendererOutput == ERendererOutput::FinalSceneColor)
		{
			Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::LumenCardCapture, Scene->NaniteShadingCommands[ENaniteMeshPass::LumenCardCapture]);
		}
	}

	FComputeLightGridOutput ComputeLightGridOutput = {};

	FCompositionLighting CompositionLighting(Views, SceneTextures, [this](int32 ViewIndex)
	{
		return GetViewPipelineState(Views[ViewIndex]).AmbientOcclusionMethod == EAmbientOcclusionMethod::SSAO;
	});

	const auto RenderOcclusionLambda = [&]() -> Froxel::FRenderer 
	{
		const int32 AsyncComputeMode = CVarSceneDepthHZBAsyncCompute.GetValueOnRenderThread();
		bool bAsyncCompute = AsyncComputeMode != 0;

		FBuildHZBAsyncComputeParams AsyncComputeParams = {};
		if (AsyncComputeMode == 2)
		{
			AsyncComputeParams.Prerequisite = ComputeLightGridOutput.CompactLinksPass;
		}

		bool bShouldGenerateFroxels = DoesVSMWantFroxels(ShaderPlatform);

		Froxel::FRenderer FroxelRenderer(bShouldGenerateFroxels, GraphBuilder, Views);

		RenderOcclusion(GraphBuilder, SceneTextures, bIsOcclusionTesting,
			bAsyncCompute ? &AsyncComputeParams : nullptr, FroxelRenderer);

		CompositionLighting.ProcessAfterOcclusion(GraphBuilder);

		return FroxelRenderer;
	};

	const bool bShouldRenderVolumetricCloudBase = ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloud = bShouldRenderVolumetricCloudBase && (!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity && !ViewFamily.EngineShowFlags.VisualizeVolumetricCloudEmptySpaceSkipping);
	const bool bShouldVisualizeVolumetricCloud = bShouldRenderVolumetricCloudBase && (!!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity || !!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudEmptySpaceSkipping);
	const bool bAsyncComputeVolumetricCloud = IsVolumetricRenderTargetEnabled() && IsVolumetricRenderTargetAsyncCompute();
	const bool bVolumetricRenderTargetRequired = bShouldRenderVolumetricCloud && !bHasRayTracedOverlay;

	Froxel::FRenderer FroxelRenderer;

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);
	FRDGTextureRef ViewFamilyDepthTexture = TryCreateViewFamilyDepthTexture(GraphBuilder, ViewFamily);
	if (RendererOutput == ERendererOutput::DepthPrepassOnly || RendererOutput == ERendererOutput::BasePass)
	{
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult = nullptr;
		if (ShouldRenderSingleLayerWaterDepthPrepass(Views))
		{
			SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, Views, SceneTextures);
		}

		bool bOcclusionBeforeBasePass = false;
		if (RendererOutput == ERendererOutput::BasePass)
		{
			// Early occlusion queries
			bOcclusionBeforeBasePass = ((DepthPass.EarlyZPassMode == EDepthDrawingMode::DDM_AllOccluders) || bIsEarlyDepthComplete);
			if (bOcclusionBeforeBasePass)
			{
				FroxelRenderer = RenderOcclusionLambda();
			}

			SceneTextures.SetupMode |= ESceneTextureSetupMode::SceneColor;
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

			for (FSceneViewExtensionRef& ViewExtension : ViewFamily.ViewExtensions)
			{
				ViewExtension->PreRenderBasePass_RenderThread(GraphBuilder, ShouldRenderPrePass() /*bDepthBufferIsPopulated*/);
			}

			RenderBasePass(*this, GraphBuilder, Views, SceneTextures, DBufferTextures, BasePassDepthStencilAccess, /*ForwardScreenSpaceShadowMaskTexture=*/nullptr, InstanceCullingManager, bNaniteEnabled, Scene->NaniteShadingCommands[ENaniteMeshPass::BasePass], NaniteRasterResults);

			if (ShouldRenderSingleLayerWater(Views))
			{
				// Virtual shadow map uniforms need to be initialized with dummy data for water.  Their initialization
				// was skipped above due to (RendererOutput == ERendererOutput::FinalSceneColor) being false.
				VirtualShadowMapArray.Initialize(GraphBuilder, Scene->GetVirtualShadowMapCache(), /*bEnableVirtualShadowMaps=*/false, ViewFamily.EngineShowFlags);

				FSceneWithoutWaterTextures SceneWithoutWaterTextures;
				RenderSingleLayerWater(GraphBuilder, Views, SceneTextures, SingleLayerWaterPrePassResult, /*bShouldRenderVolumetricCloud=*/false, SceneWithoutWaterTextures, LumenFrameTemporaries, /*bIsCameraUnderWater=*/false);
			}

			SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

			if (bUseVirtualTexturing)
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, VirtualTextureUpdate, "VirtualTextureUpdate");
				RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);

				VirtualTextureFeedbackEnd(GraphBuilder);
			}
		}

		if (!bOcclusionBeforeBasePass)
		{
			FroxelRenderer = RenderOcclusionLambda();
		}

		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.SubmitFrameStreamingRequests(GraphBuilder);
		}

		CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, ViewFamilyDepthTexture, ViewFamily, Views);
	}
	else
	{
		GVRSImageManager.PrepareImageBasedVRS(GraphBuilder, ViewFamily, SceneTextures, bAnyLumenEnabled);

		if (!IsForwardShadingEnabled(ShaderPlatform))
		{
			// Dynamic shadows are synced later when using the deferred path to make more headroom for tasks.
			FinishInitDynamicShadows(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager);
		}

		// Update groom only visible in shadow
		if (IsHairStrandsEnabled(EHairStrandsShaderType::All, Scene->GetShaderPlatform()) && RendererOutput == ERendererOutput::FinalSceneColor)
		{
			UpdateHairStrandsBookmarkParameters(Scene, Views, HairStrandsBookmarkParameters);

			// Interpolation for cards/meshes only visible in shadow needs to happen after the shadow jobs are completed
			const bool bRunHairStrands = HairStrandsBookmarkParameters.HasInstances() && (Views.Num() > 0);
			if (bRunHairStrands)
			{
				RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessCardsAndMeshesInterpolation_ShadowView, HairStrandsBookmarkParameters);
			}
		}

		// Early occlusion queries
		const bool bOcclusionBeforeBasePass = ((DepthPass.EarlyZPassMode == EDepthDrawingMode::DDM_AllOccluders) || bIsEarlyDepthComplete);

		if (bOcclusionBeforeBasePass)
		{
			FroxelRenderer = RenderOcclusionLambda();
		}

		// End early occlusion queries

		for (FSceneViewExtensionRef& ViewExtension : ViewFamily.ViewExtensions)
		{
			ViewExtension->PreRenderBasePass_RenderThread(GraphBuilder, ShouldRenderPrePass() /*bDepthBufferIsPopulated*/);
		}

		{
			const FSortedLightSetSceneInfo* SortedLightSet = GatherAndSortLightsTask.GetResult();

			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, SortLights);
			RDG_EVENT_SCOPE_STAT(GraphBuilder, SortLights, "SortLights");
			RDG_GPU_STAT_SCOPE(GraphBuilder, SortLights);

			GatherAndSortLightsTask.Wait();
			ComputeLightGridOutput = GatherLightsAndComputeLightGrid(GraphBuilder, bComputeLightGrid, *SortedLightSet);

			CSV_CUSTOM_STAT(LightCount, All,  float(SortedLightSet->SortedLights.Num()), ECsvCustomStatOp::Set);
			CSV_CUSTOM_STAT(LightCount, Batched, float(SortedLightSet->UnbatchedLightStart), ECsvCustomStatOp::Set);
			CSV_CUSTOM_STAT(LightCount, Unbatched, float(SortedLightSet->SortedLights.Num()) - float(SortedLightSet->UnbatchedLightStart), ECsvCustomStatOp::Set);
		}

		LightFunctionAtlas.RenderLightFunctionAtlas(GraphBuilder, Views);

		// Run before RenderSkyAtmosphereLookUpTables for cloud shadows to be valid.
		InitVolumetricCloudsForViews(GraphBuilder, bShouldRenderVolumetricCloudBase, InstanceCullingManager);

		if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforeOcclusion && bShouldRenderSkyAtmosphere)
		{
			// Generate the Sky/Atmosphere look up tables
			RenderSkyAtmosphereLookUpTables(GraphBuilder, /* out */ SkyAtmospherePendingRDGResources);

			SkyAtmospherePendingRDGResources.CommitToSceneAndViewUniformBuffers(GraphBuilder, /* out */ ExternalAccessQueue);
			ExternalAccessQueue.Submit(GraphBuilder);
		}

		BeginAsyncDistanceFieldShadowProjections(GraphBuilder, SceneTextures, InitViewTaskDatas.DynamicShadows);

		// Run local fog volume culling before base pass and after HZB generation to benefit from more culling.
		InitLocalFogVolumesForViews(Scene, Views, ViewFamily, GraphBuilder, bShouldRenderVolumetricFog, false /*bool bUseHalfResLocalFogVolume*/);

		if (bShouldRenderVolumetricCloudBase)
		{
			InitVolumetricRenderTargetForViews(GraphBuilder, Views);
		}
		else
		{
			ResetVolumetricRenderTargetForViews(GraphBuilder, Views);
		}

		// Generate sky LUTs
		// TODO: Valid shadow maps (for volumetric light shafts) have not yet been generated at this point in the frame. Need to resolve dependency ordering!
		// This also must happen before the BasePass for Sky material to be able to sample valid LUTs.
		if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforeBasePass && bShouldRenderSkyAtmosphere)
		{
			// Generate the Sky/Atmosphere look up tables
			RenderSkyAtmosphereLookUpTables(GraphBuilder, /* out */ SkyAtmospherePendingRDGResources);

			SkyAtmospherePendingRDGResources.CommitToSceneAndViewUniformBuffers(GraphBuilder, /* out */ ExternalAccessQueue);
			ExternalAccessQueue.Submit(GraphBuilder);
		}
		else if (SkyAtmospherePassLocation == ESkyAtmospherePassLocation::BeforePrePass && bShouldRenderSkyAtmosphere)
		{
			SkyAtmospherePendingRDGResources.CommitToSceneAndViewUniformBuffers(GraphBuilder, /* out */ ExternalAccessQueue);
			ExternalAccessQueue.Submit(GraphBuilder);
		}

		// Capture the SkyLight using the SkyAtmosphere and VolumetricCloud component if available.
		const bool bRealTimeSkyCaptureEnabled = Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled && Views.Num() > 0 && ViewFamily.EngineShowFlags.SkyLighting;
		if (bRealTimeSkyCaptureEnabled)
		{
			FViewInfo& MainView = Views[0];
			Scene->AllocateAndCaptureFrameSkyEnvMap(GraphBuilder, *this, MainView, bShouldRenderSkyAtmosphere, bShouldRenderVolumetricCloud, InstanceCullingManager, ExternalAccessQueue);
		}

		const ECustomDepthPassLocation CustomDepthPassLocation = GetCustomDepthPassLocation(ShaderPlatform);
		if (CustomDepthPassLocation == ECustomDepthPassLocation::BeforeBasePass)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass_BeforeBasePass);
			if (RenderCustomDepthPass(GraphBuilder, SceneTextures.CustomDepth, SceneTextures.GetSceneTextureShaderParameters(FeatureLevel), NaniteRasterResults, PrimaryNaniteViews))
			{
				SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
				SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
			}
		}

		// Lumen updates need access to sky atmosphere LUT.
		ExternalAccessQueue.Submit(GraphBuilder);

		UpdateLumenScene(GraphBuilder, LumenFrameTemporaries);

		FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture = nullptr;
		FRDGTextureRef QuarterResolutionDepthMinMaxTexture = nullptr;
		bool bQuarterResMinMaxDepthRequired = bShouldRenderVolumetricCloud && ShouldVolumetricCloudTraceWithMinMaxDepth(Views);

		auto GenerateQuarterResDepthMinMaxTexture = [&](auto& GraphBuilder, auto& Views, auto& InputTexture)
		{
			if (bQuarterResMinMaxDepthRequired)
			{
				check(InputTexture != nullptr);	// Must receive a valid texture
				check(QuarterResolutionDepthMinMaxTexture == nullptr);	// Only generate it once
				return CreateQuarterResolutionDepthMinAndMax(GraphBuilder, Views, InputTexture);
			}
			return FRDGTextureRef(nullptr);
		};
		
		FRDGTextureRef ForwardScreenSpaceShadowMaskTexture = nullptr;
		FRDGTextureRef ForwardScreenSpaceShadowMaskHairTexture = nullptr;
		bool bShadowMapsRenderedEarly = false;
		if (IsForwardShadingEnabled(ShaderPlatform))
		{
			// With forward shading we need to render shadow maps early
			ensureMsgf(!VirtualShadowMapArray.IsEnabled(), TEXT("Virtual shadow maps are not supported in the forward shading path"));
			RenderShadowDepthMaps(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager, ExternalAccessQueue);
			bShadowMapsRenderedEarly = true;

			if (bHairStrandsEnable)
			{
				RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessStrandsInterpolation, HairStrandsBookmarkParameters);
				if (!bHasRayTracedOverlay)
				{
					RenderHairPrePass(GraphBuilder, Scene, Views, InstanceCullingManager, HairStrandsBookmarkParameters.InstancesVisibilityType);
					RenderHairBasePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager);
				}
			}

			RenderForwardShadowProjections(GraphBuilder, SceneTextures, ForwardScreenSpaceShadowMaskTexture, ForwardScreenSpaceShadowMaskHairTexture);

			// With forward shading we need to render volumetric fog before the base pass
			ComputeVolumetricFog(GraphBuilder, SceneTextures);
		}
		else if ( CVarShadowMapsRenderEarly.GetValueOnRenderThread() )
		{
			// Disable early shadows if VSM is enabled, but warn
			ensureMsgf(!VirtualShadowMapArray.IsEnabled(), TEXT("Virtual shadow maps are not supported with r.shadow.ShadowMapsRenderEarly. Early shadows will be disabled"));
			if (!VirtualShadowMapArray.IsEnabled())
			{
				RenderShadowDepthMaps(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager, ExternalAccessQueue);
				bShadowMapsRenderedEarly = true;
			}
		}

		ExternalAccessQueue.Submit(GraphBuilder);

		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, DeferredShadingSceneRenderer_DBuffer);
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_DBuffer);
			CompositionLighting.ProcessBeforeBasePass(GraphBuilder, DBufferTextures, InstanceCullingManager, Scene->SubstrateSceneData);
		}
		
		if (IsForwardShadingEnabled(ShaderPlatform))
		{
			RenderIndirectCapsuleShadows(GraphBuilder, SceneTextures);
		}

		FTranslucencyLightingVolumeTextures TranslucencyLightingVolumeTextures;

		if (bRenderDeferredLighting && GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
		{
			TranslucencyLightingVolumeTextures.Init(GraphBuilder, Views, ERDGPassFlags::AsyncCompute);
		}

		FRDGBufferRef DynamicGeometryScratchBuffer = nullptr;
#if RHI_RAYTRACING
		// Async AS builds can potentially overlap with BasePass.
		bool bNeedToWaitForRayTracingScene = DispatchRayTracingWorldUpdates(GraphBuilder, DynamicGeometryScratchBuffer);

		/** Should be called somewhere before "WaitForRayTracingScene" */
		SetupRayTracingLightDataForViews(GraphBuilder);
#endif

		if (!bHasRayTracedOverlay)
		{
#if RHI_RAYTRACING
			// Lumen scene lighting requires ray tracing scene to be ready if HWRT shadows are desired
			if (bNeedToWaitForRayTracingScene && Lumen::UseHardwareRayTracedSceneLighting(ViewFamily))
			{
				WaitForRayTracingScene(GraphBuilder);
				bNeedToWaitForRayTracingScene = false;
			}
#endif

			LLM_SCOPE_BYTAG(Lumen);
			BeginGatheringLumenSurfaceCacheFeedback(GraphBuilder, Views[0], LumenFrameTemporaries);
			RenderLumenSceneLighting(GraphBuilder, LumenFrameTemporaries, InitViewTaskDatas.LumenDirectLighting);
		}

		{
			if (!bHasRayTracedOverlay)
			{
				RenderBasePass(*this, GraphBuilder, Views, SceneTextures, DBufferTextures, BasePassDepthStencilAccess, ForwardScreenSpaceShadowMaskTexture, InstanceCullingManager, bNaniteEnabled, Scene->NaniteShadingCommands[ENaniteMeshPass::BasePass], NaniteRasterResults);
			}

			if (!bAllowReadOnlyDepthBasePass)
			{
				AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
			}

			if (bNaniteEnabled)
			{
				if (GNaniteShowStats != 0)
				{
					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						const FViewInfo& View = Views[ViewIndex];
						if (IStereoRendering::IsAPrimaryView(View))
						{
							Nanite::PrintStats(GraphBuilder, View);
						}
					}
				}

				if (bVisualizeNanite)
				{
					FNanitePickingFeedback PickingFeedback = { 0 };

					Nanite::AddVisualizationPasses(
						GraphBuilder,
						Scene,
						SceneTextures,
						ViewFamily.EngineShowFlags,
						Views,
						NaniteRasterResults,
						PickingFeedback,
						VirtualShadowMapArray
					);

					OnGetOnScreenMessages.AddLambda([this, PickingFeedback, RenderFlags = NaniteRasterResults[0].RenderFlags, ScenePtr = Scene](FScreenMessageWriter& ScreenMessageWriter)->void
					{
						Nanite::DisplayPicking(ScenePtr, PickingFeedback, RenderFlags, ScreenMessageWriter);
					});
				}
			}

			// VisualizeVirtualShadowMap TODO
		}

		FRDGTextureRef ExposureIlluminanceSetup = nullptr;
		if (!bHasRayTracedOverlay)
		{
			// Extract emissive from SceneColor (before lighting is applied)
			ExposureIlluminanceSetup = AddSetupExposureIlluminancePass(GraphBuilder, Views, SceneTextures);
		}

		if (ViewFamily.EngineShowFlags.VisualizeLightCulling)
		{
			FRDGTextureRef VisualizeLightCullingTexture = GraphBuilder.CreateTexture(SceneTextures.Color.Target->Desc, TEXT("SceneColorVisualizeLightCulling"));
			AddClearRenderTargetPass(GraphBuilder, VisualizeLightCullingTexture, FLinearColor::Transparent);
			SceneTextures.Color.Target = VisualizeLightCullingTexture;

			// When not in MSAA, assign to both targets.
			if (SceneTexturesConfig.NumSamples == 1)
			{
				SceneTextures.Color.Resolve = SceneTextures.Color.Target;
			}
		}

		if (bUseGBuffer)
		{
			// mark GBufferA for saving for next frame if it's needed
			ExtractNormalsForNextFrameReprojection(GraphBuilder, SceneTextures, Views);
		}

		// Rebuild scene textures to include GBuffers.
		SceneTextures.SetupMode |= ESceneTextureSetupMode::GBuffers;
		if (bShouldRenderVelocities && (bBasePassCanOutputVelocity || Scene->EarlyZPassMode == DDM_AllOpaqueNoVelocity))
		{
			SceneTextures.SetupMode |= ESceneTextureSetupMode::SceneVelocity;
		}
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		if (bRealTimeSkyCaptureEnabled)
		{
			Scene->ValidateSkyLightRealTimeCapture(GraphBuilder, Views[0], SceneTextures.Color.Target);
		}

		VisualizeVolumetricLightmap(GraphBuilder, SceneTextures);

		// Occlusion after base pass
		if (!bOcclusionBeforeBasePass)
		{
			FroxelRenderer = RenderOcclusionLambda();
		}

		// End occlusion after base

		if (!bUseGBuffer)
		{
			AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);
		}

		// Render hair
		if (bHairStrandsEnable && !IsForwardShadingEnabled(ShaderPlatform))
		{
			RunHairStrandsBookmark(GraphBuilder, EHairStrandsBookmark::ProcessStrandsInterpolation, HairStrandsBookmarkParameters);
			if (!bHasRayTracedOverlay)
			{
				RenderHairPrePass(GraphBuilder, Scene, Views, InstanceCullingManager, HairStrandsBookmarkParameters.InstancesVisibilityType);
				RenderHairBasePass(GraphBuilder, Scene, SceneTextures, Views, InstanceCullingManager);
			}
		}

		if (ShouldRenderHeterogeneousVolumes(Scene) && !bHasRayTracedOverlay)
		{
			RenderHeterogeneousVolumeShadows(GraphBuilder, SceneTextures);
		}

		// Post base pass for material classification
		// This needs to run before virtual shadow map, in order to have ready&cleared classified SSS data
		if (Substrate::IsSubstrateEnabled() && !bHasRayTracedOverlay)
		{
			Substrate::AddSubstrateMaterialClassificationPass(GraphBuilder, SceneTextures, DBufferTextures, Views);
			Substrate::AddSubstrateDBufferPass(GraphBuilder, SceneTextures, DBufferTextures, Views);
		}

		// Copy lighting channels out of stencil before deferred decals which overwrite those values
		TArray<FRDGTextureRef, TInlineAllocator<2>> NaniteShadingMask;
		if (bNaniteEnabled && Views.Num() > 0)
		{
			check(Views.Num() == NaniteRasterResults.Num());
			for (const Nanite::FRasterResults& Results : NaniteRasterResults)
			{
				NaniteShadingMask.Add(Results.ShadingMask);
			}
		}
		FRDGTextureRef LightingChannelsTexture = CopyStencilToLightingChannelTexture(GraphBuilder, SceneTextures.Stencil, NaniteShadingMask);

		// Single layer water depth prepass. Needs to run before VSM page allocation.
		const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult = nullptr;

		const bool bShouldRenderSingleLayerWaterDepthPrepass = !bHasRayTracedOverlay && ShouldRenderSingleLayerWaterDepthPrepass(Views);
		if (bShouldRenderSingleLayerWaterDepthPrepass)
		{
			SingleLayerWaterPrePassResult = RenderSingleLayerWaterDepthPrepass(GraphBuilder, Views, SceneTextures);
		}

		FAsyncLumenIndirectLightingOutputs AsyncLumenIndirectLightingOutputs;

		GraphBuilder.FlushSetupQueue();

		// Shadows, lumen and fog after base pass
		if (!bHasRayTracedOverlay)
		{
#if RHI_RAYTRACING
			// When Lumen HWRT is running async we need to wait for ray tracing scene before dispatching the work
			if (bNeedToWaitForRayTracingScene && Lumen::UseAsyncCompute(ViewFamily) && Lumen::UseHardwareInlineRayTracing(ViewFamily))
			{
				WaitForRayTracingScene(GraphBuilder);
				bNeedToWaitForRayTracingScene = false;
			}
#endif // RHI_RAYTRACING

			DispatchAsyncLumenIndirectLightingWork(
				GraphBuilder,
				CompositionLighting,
				SceneTextures,
				InstanceCullingManager,
				LumenFrameTemporaries,
				InitViewTaskDatas.DynamicShadows,
				LightingChannelsTexture,
				/*bHasLumenLights*/ false,
				AsyncLumenIndirectLightingOutputs);

			// Kick off volumetric clouds async dispatch after Lumen
			// Lumen has a dependency on the opaque so should run first
			// Volumetric Clouds have a depedency on translucent, so should run second and overlap opaque work after Lumen async is done
			if (bShouldRenderVolumetricCloud && bAsyncComputeVolumetricCloud)
			{
				HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
				QuarterResolutionDepthMinMaxTexture = GenerateQuarterResDepthMinMaxTexture(GraphBuilder, Views, HalfResolutionDepthCheckerboardMinMaxTexture);

				bool bSkipVolumetricRenderTarget = false;
				bool bSkipPerPixelTracing = true;
				bool bAccumulateAlphaHoldOut = false;
				RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, bAccumulateAlphaHoldOut,
					HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, true, InstanceCullingManager);
			}

			// If we haven't already rendered shadow maps, render them now (due to forward shading or r.shadow.ShadowMapsRenderEarly)
			if (!bShadowMapsRenderedEarly)
			{
				if (VirtualShadowMapArray.IsEnabled())
				{
					// TODO: actually move this inside RenderShadowDepthMaps instead of this extra scope to make it 1:1 with profiling captures/traces
					RDG_EVENT_SCOPE_STAT(GraphBuilder, ShadowDepths, "ShadowDepths");
					RDG_GPU_STAT_SCOPE(GraphBuilder, ShadowDepths);

					ensureMsgf(AreLightsInLightGrid(), TEXT("Virtual shadow map setup requires local lights to be injected into the light grid (this may be caused by 'r.LightCulling.Quality=0')."));

					FFrontLayerTranslucencyData FrontLayerTranslucencyData = RenderFrontLayerTranslucency(GraphBuilder, Views, SceneTextures, true /*VSM page marking*/);

					VirtualShadowMapArray.BuildPageAllocations(GraphBuilder,
						GetActiveSceneTextures(),
						Views,
						*GatherAndSortLightsTask.GetResult(),
						VisibleLightInfos,
						SingleLayerWaterPrePassResult,
						FrontLayerTranslucencyData,
						FroxelRenderer,
						ShadowSceneRenderer->AreAnyLocalLightsPreset()
					);
				}

				RenderShadowDepthMaps(GraphBuilder, InitViewTaskDatas.DynamicShadows, InstanceCullingManager, ExternalAccessQueue);
			}
			CheckShadowDepthRenderCompleted();

#if RHI_RAYTRACING
			// Lumen scene lighting requires ray tracing scene to be ready if HWRT shadows are desired
			if (bNeedToWaitForRayTracingScene && Lumen::UseHardwareRayTracedSceneLighting(ViewFamily))
			{
				WaitForRayTracingScene(GraphBuilder);
				bNeedToWaitForRayTracingScene = false;
			}
#endif // RHI_RAYTRACING
		}

		ExternalAccessQueue.Submit(GraphBuilder);

		// End shadow and fog after base pass

		if (bNaniteEnabled)
		{
			// Needs doing after shadows such that the checks for shadow atlases etc work.
			Nanite::ListStatFilters(this);
		}

		if (bUpdateNaniteStreaming)
		{
			Nanite::GStreamingManager.SubmitFrameStreamingRequests(GraphBuilder);
		}

		{
			FVirtualShadowMapArrayCacheManager* CacheManager = VirtualShadowMapArray.CacheManager;
			if (CacheManager)
			{
				// Do this even if VSMs are disabled this frame to clean up any previously extracted data
				CacheManager->ExtractFrameData(
					GraphBuilder,
					VirtualShadowMapArray,
					*this,
					ViewFamily.EngineShowFlags.VirtualShadowMapPersistentData);
			}
		}

		if (CustomDepthPassLocation == ECustomDepthPassLocation::AfterBasePass)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_CustomDepthPass_AfterBasePass);
			if (RenderCustomDepthPass(GraphBuilder, SceneTextures.CustomDepth, SceneTextures.GetSceneTextureShaderParameters(FeatureLevel), NaniteRasterResults, PrimaryNaniteViews))
			{
				SceneTextures.SetupMode |= ESceneTextureSetupMode::CustomDepth;
				SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
			}
		}

		// If we are not rendering velocities in depth or base pass then do that here.
		if (bShouldRenderVelocities && !bBasePassCanOutputVelocity && (Scene->EarlyZPassMode != DDM_AllOpaqueNoVelocity))
		{
			RenderVelocities(GraphBuilder, Views, SceneTextures, EVelocityPass::Opaque, bHairStrandsEnable);
		}

		// Pre-lighting composition lighting stage
		// e.g. deferred decals, SSAO
		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, AfterBasePass);
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_AfterBasePass);

			if (!IsForwardShadingEnabled(ShaderPlatform))
			{
				AddResolveSceneDepthPass(GraphBuilder, Views, SceneTextures.Depth);
			}

			const FCompositionLighting::EProcessAfterBasePassMode Mode = AsyncLumenIndirectLightingOutputs.bHasDrawnBeforeLightingDecals ?
				FCompositionLighting::EProcessAfterBasePassMode::SkipBeforeLightingDecals : FCompositionLighting::EProcessAfterBasePassMode::All;

			CompositionLighting.ProcessAfterBasePass(GraphBuilder, InstanceCullingManager, Mode, Scene->SubstrateSceneData);
		}

		// Rebuild scene textures to include velocity, custom depth, and SSAO.
		SceneTextures.SetupMode |= ESceneTextureSetupMode::All;
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		if (!IsForwardShadingEnabled(ShaderPlatform))
		{
			// Clear stencil to 0 now that deferred decals are done using what was setup in the base pass.
			AddClearStencilPass(GraphBuilder, SceneTextures.Depth.Target);
		}

#if RHI_RAYTRACING
		// If Lumen did not force an earlier ray tracing scene sync, we must wait for it here.
		if (bNeedToWaitForRayTracingScene)
		{
			WaitForRayTracingScene(GraphBuilder);
			bNeedToWaitForRayTracingScene = false;
		}
#endif // RHI_RAYTRACING

		GraphBuilder.FlushSetupQueue();

		if (bRenderDeferredLighting)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, RenderDeferredLighting, "RenderDeferredLighting");
			RDG_GPU_STAT_SCOPE(GraphBuilder, RenderDeferredLighting);
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderLighting);

			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_Lighting);
			SCOPED_NAMED_EVENT(RenderLighting, FColor::Emerald);

			TArray<FRDGTextureRef> DynamicBentNormalAOTextures;

			RenderDiffuseIndirectAndAmbientOcclusion(
				GraphBuilder,
				SceneTextures,
				LumenFrameTemporaries,
				LightingChannelsTexture,
				/*bHasLumenLights*/ false,
				/* bCompositeRegularLumenOnly = */ false,
				/* bIsVisualizePass = */ false,
				AsyncLumenIndirectLightingOutputs);

			// These modulate the scenecolor output from the basepass, which is assumed to be indirect lighting
			RenderIndirectCapsuleShadows(GraphBuilder, SceneTextures);

			// These modulate the scene color output from the base pass, which is assumed to be indirect lighting
			RenderDFAOAsIndirectShadowing(GraphBuilder, SceneTextures, DynamicBentNormalAOTextures);

			// Clear the translucent lighting volumes before we accumulate
			if ((GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute) == false)
			{
				TranslucencyLightingVolumeTextures.Init(GraphBuilder, Views, ERDGPassFlags::Compute);
			}

#if RHI_RAYTRACING
			// Only used by ray traced shadows
			if (IsRayTracingEnabled() && Scene->bHasLightsWithRayTracedShadows && Views[0].IsRayTracingAllowedForView())
			{
				RenderDitheredLODFadingOutMask(GraphBuilder, Views[0], SceneTextures.Depth.Target);
			}
#endif

			const FSortedLightSetSceneInfo& SortedLightSet = *GatherAndSortLightsTask.GetResult();

			RenderLights(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, LightingChannelsTexture, SortedLightSet);

			if (SortedLightSet.MegaLightsLightStart < SortedLightSet.SortedLights.Num())
			{
				RenderMegaLights(
					GraphBuilder,
					SceneTextures);
			}

			// Copy depth history without water and translucency for ray traced lighting denoising
			StoreStochasticLightingSceneHistory(GraphBuilder, LumenFrameTemporaries, SceneTextures);

			InjectTranslucencyLightingVolumeAmbientCubemap(GraphBuilder, Views, TranslucencyLightingVolumeTextures);
			FilterTranslucencyLightingVolume(GraphBuilder, Views, TranslucencyLightingVolumeTextures);

			// Do DiffuseIndirectComposite after Lights so that async Lumen work can overlap
			RenderDiffuseIndirectAndAmbientOcclusion(
				GraphBuilder,
				SceneTextures,
				LumenFrameTemporaries,
				LightingChannelsTexture,
				/*bHasLumenLights*/ false,
				/* bCompositeRegularLumenOnly = */ true,
				/* bIsVisualizePass = */ false,
				AsyncLumenIndirectLightingOutputs);

			// Render diffuse sky lighting and reflections that only operate on opaque pixels
			RenderDeferredReflectionsAndSkyLighting(GraphBuilder, SceneTextures, LumenFrameTemporaries, DynamicBentNormalAOTextures);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			// Renders debug visualizations for global illumination plugins
			RenderGlobalIlluminationPluginVisualizations(GraphBuilder, LightingChannelsTexture);
#endif

			AddSubsurfacePass(GraphBuilder, SceneTextures, Views);

			Substrate::AddSubstrateOpaqueRoughRefractionPasses(GraphBuilder, SceneTextures, Views);

			{
				RenderHairStrandsSceneColorScattering(GraphBuilder, SceneTextures.Color.Target, Scene, Views);
			}

		#if RHI_RAYTRACING
			if (ShouldRenderRayTracingSkyLight(Scene->SkyLight, Scene->GetShaderPlatform()) 
				//@todo - integrate RenderRayTracingSkyLight into RenderDiffuseIndirectAndAmbientOcclusion
				&& GetViewPipelineState(Views[0]).DiffuseIndirectMethod != EDiffuseIndirectMethod::Lumen
				&& ViewFamily.EngineShowFlags.GlobalIllumination)
			{
				FRDGTextureRef SkyLightTexture = nullptr;
				FRDGTextureRef SkyLightHitDistanceTexture = nullptr;
				RenderRayTracingSkyLight(GraphBuilder, SceneTextures.Color.Target, SkyLightTexture, SkyLightHitDistanceTexture);
				CompositeRayTracingSkyLight(GraphBuilder, SceneTextures, SkyLightTexture, SkyLightHitDistanceTexture);
			}
		#endif

			if (Substrate::IsSubstrateEnabled())
			{
				// Now remove all the Substrate tile stencil tags used by deferred tiled light passes. Make later marks such as responssive AA works.
				AddClearStencilPass(GraphBuilder, SceneTextures.Depth.Target);
			}
		}
		else if (HairStrands::HasViewHairStrandsData(Views) && ViewFamily.EngineShowFlags.Lighting)
		{
			const FSortedLightSetSceneInfo& SortedLightSet = *GatherAndSortLightsTask.GetResult();
			RenderLightsForHair(GraphBuilder, SceneTextures, SortedLightSet, ForwardScreenSpaceShadowMaskHairTexture, LightingChannelsTexture);
			RenderDeferredReflectionsAndSkyLightingHair(GraphBuilder);
		}

		// Volumetric fog after Lumen GI and shadow depths
		if (!IsForwardShadingEnabled(ShaderPlatform) && !bHasRayTracedOverlay)
		{
			ComputeVolumetricFog(GraphBuilder, SceneTextures);
		}

		if (ShouldRenderHeterogeneousVolumes(Scene) && !bHasRayTracedOverlay)
		{
			RenderHeterogeneousVolumes(GraphBuilder, SceneTextures);
		}

		GraphBuilder.FlushSetupQueue();

		if (bShouldRenderVolumetricCloud && IsVolumetricRenderTargetEnabled() && HalfResolutionDepthCheckerboardMinMaxTexture==nullptr && !bHasRayTracedOverlay)
		{
			HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
			QuarterResolutionDepthMinMaxTexture = GenerateQuarterResDepthMinMaxTexture(GraphBuilder, Views, HalfResolutionDepthCheckerboardMinMaxTexture);
		}

		if (bShouldRenderVolumetricCloud && !bHasRayTracedOverlay)
		{
			if (!bAsyncComputeVolumetricCloud)
			{
				// Generate the volumetric cloud render target
				bool bSkipVolumetricRenderTarget = false;
				bool bSkipPerPixelTracing = true;
				bool bAccumulateAlphaHoldOut = false;
				RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, bAccumulateAlphaHoldOut,
					HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, false, InstanceCullingManager);
			}
			// Reconstruct the volumetric cloud render target to be ready to compose it over the scene
			ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, bAsyncComputeVolumetricCloud);
		}

		TArray<FScreenPassTexture, TInlineAllocator<4>> TSRFlickeringInputTextures;
		if (!bHasRayTracedOverlay)
		{
			// Extract TSR's moire heuristic luminance before rendering translucency into the scene color.
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				FViewInfo& View = Views[ViewIndex];
				if (NeedTSRMoireLuma(View))
				{
					if (TSRFlickeringInputTextures.Num() == 0)
					{
						TSRFlickeringInputTextures.SetNum(Views.Num());
					}

					TSRFlickeringInputTextures[ViewIndex] = AddTSRMeasureFlickeringLuma(GraphBuilder, View.ShaderMap, FScreenPassTexture(SceneTextures.Color.Target, View.ViewRect));
				}
			}
		}

		const bool bShouldRenderTranslucency = !bHasRayTracedOverlay && ShouldRenderTranslucency();

		// Union of all translucency view render flags.
		ETranslucencyView TranslucencyViewsToRender = bShouldRenderTranslucency ? GetTranslucencyViews(Views) : ETranslucencyView::None;

		FTranslucencyPassResourcesMap TranslucencyResourceMap(Views.Num());

		const bool bIsCameraUnderWater = EnumHasAnyFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater);
		FRDGTextureRef LightShaftOcclusionTexture = nullptr;
		const bool bShouldRenderSingleLayerWater = !bHasRayTracedOverlay && ShouldRenderSingleLayerWater(Views);
		FSceneWithoutWaterTextures SceneWithoutWaterTextures;
		auto RenderLigthShaftSkyFogAndCloud = [&]()
		{
			// Draw Lightshafts
			if (!bHasRayTracedOverlay && ViewFamily.EngineShowFlags.LightShafts)
			{
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftOcclusion);
				LightShaftOcclusionTexture = RenderLightShaftOcclusion(GraphBuilder, SceneTextures);
			}

			// Draw the sky atmosphere
			if (!bHasRayTracedOverlay && bShouldRenderSkyAtmosphere && !IsForwardShadingEnabled(ShaderPlatform))
			{
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderSkyAtmosphere);
				RenderSkyAtmosphere(GraphBuilder, SceneTextures);
			}

			// Draw fog.
			bool bHeightFogHasComposedLocalFogVolume = false;
			if (!bHasRayTracedOverlay && ShouldRenderFog(ViewFamily))
			{
				RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderFog);
				SCOPED_NAMED_EVENT(RenderFog, FColor::Emerald);
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFog);
				const bool bFogComposeLocalFogVolumes = (bShouldRenderLocalFogVolumeInVolumetricFog && bShouldRenderVolumetricFog) || bShouldRenderLocalFogVolumeDuringHeightFogPass;
				RenderFog(GraphBuilder, SceneTextures, LightShaftOcclusionTexture, bFogComposeLocalFogVolumes);
				bHeightFogHasComposedLocalFogVolume = bFogComposeLocalFogVolumes;
			}

			// Local Fog Volumes (LFV) rendering order is first HeightFog, then LFV, then volumetric fog on top.
			// LFVs are rendered as part of the regular height fog + volumetric fog pass when volumetric fog is enabled and it is requested to voxelise LFVs into volumetric fog.
			// Otherwise, they are rendered in an independent pass (this for instance make it independent of the near clip plane optimization).
			if (!bHasRayTracedOverlay && !bHeightFogHasComposedLocalFogVolume)
			{
				RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderLocalFogVolume);
				SCOPED_NAMED_EVENT(RenderLocalFogVolume, FColor::Emerald);
				SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLocalFogVolume);
				RenderLocalFogVolume(Scene, Views, ViewFamily, GraphBuilder, SceneTextures, LightShaftOcclusionTexture);
			}

			// After the height fog, Draw volumetric clouds (having fog applied on them already) when using per pixel tracing,
			if (!bHasRayTracedOverlay && bShouldRenderVolumetricCloud)
			{
				bool bSkipVolumetricRenderTarget = true;
				bool bSkipPerPixelTracing = false;
				bool bAccumulateAlphaHoldOut = false;
				RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, bAccumulateAlphaHoldOut,
					HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, false, InstanceCullingManager);
			}

			// Or composite the off screen buffer over the scene.
			if (bVolumetricRenderTargetRequired)
			{
				const bool bComposeWithWater = bIsCameraUnderWater ? false : bShouldRenderSingleLayerWater;
				ComposeVolumetricRenderTargetOverScene(
					GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target,
					bComposeWithWater,
					SceneWithoutWaterTextures, SceneTextures);

				if (IsPrimitiveAlphaHoldoutEnabledForAnyView(Views))
				{
					// When alpha is enabled to work with holdout. We need another full screen tracing pass to update the alpha channel containing the "holdout alpha throughput".
					// Alpha hold out only works when using r.volumetricrendertarget.mode 3 which is the mode use by MRQ.
					bool bSkipVolumetricRenderTarget = true;
					bool bSkipPerPixelTracing = false;
					bool bAccumulateAlphaHoldOut = true;
					RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, bAccumulateAlphaHoldOut,
						HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, false, InstanceCullingManager);
				}
			}
		};

		if (bShouldRenderSingleLayerWater)
		{
			if (bIsCameraUnderWater)
			{
				RenderLigthShaftSkyFogAndCloud();

				RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderTranslucency);
				SCOPED_NAMED_EVENT(RenderTranslucency, FColor::Emerald);
				SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);
				const bool bStandardTranslucentCanRenderSeparate = false;
				FRDGTextureMSAA SharedDepthTexture;
				RenderTranslucency(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, &TranslucencyResourceMap, ETranslucencyView::UnderWater, InstanceCullingManager, bStandardTranslucentCanRenderSeparate, SharedDepthTexture);
				EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::UnderWater);
			}

			RenderSingleLayerWater(GraphBuilder, Views, SceneTextures, SingleLayerWaterPrePassResult, bShouldRenderVolumetricCloud, SceneWithoutWaterTextures, LumenFrameTemporaries, bIsCameraUnderWater);

			// Replace main depth texture with the output of the SLW depth prepass which contains the scene + water.
			// Note: Stencil now has all water bits marked with 1. As long as no other passes after this point want to read the depth buffer,
			// a stencil clear should not be necessary here.
			if (SingleLayerWaterPrePassResult)
			{
				SceneTextures.Depth = SingleLayerWaterPrePassResult->DepthPrepassTexture;
			}
		}

		// Rebuild scene textures to include scene color.
		SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);

		if (!bIsCameraUnderWater)
		{
			RenderLigthShaftSkyFogAndCloud();
		}

		FRDGTextureRef ExposureIlluminance = nullptr;
		if (!bHasRayTracedOverlay)
		{
			ExposureIlluminance = AddCalculateExposureIlluminancePass(GraphBuilder, Views, SceneTextures, TranslucencyLightingVolumeTextures, ExposureIlluminanceSetup);
		}

		RenderOpaqueFX(GraphBuilder, GetSceneViews(), GetSceneUniforms(), FXSystem, FeatureLevel, SceneTextures.UniformBuffer);

		FRendererModule& RendererModule = static_cast<FRendererModule&>(GetRendererModule());
		RendererModule.RenderPostOpaqueExtensions(GraphBuilder, Views, SceneTextures);

		if (Scene->GPUScene.ExecuteDeferredGPUWritePass(GraphBuilder, Views, EGPUSceneGPUWritePass::PostOpaqueRendering))
		{
			InstanceCullingManager.BeginDeferredCulling(GraphBuilder, Scene->GPUScene);
		}

		if (GetHairStrandsComposition() == EHairStrandsCompositionType::BeforeTranslucent)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, HairRendering, "HairRendering");
			RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);
			RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
		}

#if DEBUG_ALPHA_CHANNEL
		if (ShouldMakeDistantGeometryTranslucent())
		{
			SceneTextures.Color = MakeDistanceGeometryTranslucent(GraphBuilder, Views, SceneTextures);
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
		}
#endif

		// Experimental voxel test code
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
		
			Nanite::DrawVisibleBricks( GraphBuilder, *Scene, View, SceneTextures );
		}

		// Composite Heterogeneous Volumes
		if (!bHasRayTracedOverlay && ShouldRenderHeterogeneousVolumes(Scene) &&
			(GetHeterogeneousVolumesComposition() == EHeterogeneousVolumesCompositionType::BeforeTranslucent))
		{
			CompositeHeterogeneousVolumes(GraphBuilder, SceneTextures);
		}

		// Draw translucency.
		FRDGTextureMSAA TranslucencySharedDepthTexture;
		if (!bHasRayTracedOverlay && TranslucencyViewsToRender != ETranslucencyView::None)
		{
			RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderTranslucency);
			SCOPED_NAMED_EVENT(RenderTranslucency, FColor::Emerald);
			SCOPE_CYCLE_COUNTER(STAT_TranslucencyDrawTime);

			RDG_EVENT_SCOPE(GraphBuilder, "Translucency");

			// Raytracing doesn't need the distortion effect.
			const bool bShouldRenderDistortion = TranslucencyViewsToRender != ETranslucencyView::RayTracing && ShouldRenderDistortion();

#if RHI_RAYTRACING
			if (EnumHasAnyFlags(TranslucencyViewsToRender, ETranslucencyView::RayTracing))
			{
				RenderRayTracingTranslucency(GraphBuilder, SceneTextures.Color);
				EnumRemoveFlags(TranslucencyViewsToRender, ETranslucencyView::RayTracing);
			}
#endif

			// Lumen/VSM translucent front layer
			FFrontLayerTranslucencyData FrontLayerTranslucencyData = RenderFrontLayerTranslucency(GraphBuilder, Views, SceneTextures, false /*VSM page marking*/);
			for (FViewInfo& View : Views)
			{
				if (GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen)
				{
					RenderLumenFrontLayerTranslucencyReflections(GraphBuilder, View, SceneTextures, LumenFrameTemporaries, FrontLayerTranslucencyData);
				}
			}

			// Sort objects' triangles
			for (FViewInfo& View : Views)
			{
				if (OIT::IsSortedTrianglesEnabled(View.GetShaderPlatform()))
				{
					OIT::AddSortTrianglesPass(GraphBuilder, View, Scene->OITSceneData, FTriangleSortingOrder::BackToFront);
				}
			}

			{
				// Render all remaining translucency views.
				const bool bStandardTranslucentCanRenderSeparate = bShouldRenderDistortion; // It is only needed to render standard translucent as separate when there is distortion (non self distortion of transmittance/specular/etc.)
				RenderTranslucency(GraphBuilder, SceneTextures, TranslucencyLightingVolumeTextures, &TranslucencyResourceMap, TranslucencyViewsToRender, InstanceCullingManager, bStandardTranslucentCanRenderSeparate, TranslucencySharedDepthTexture);
			}

			// Compose hair before velocity/distortion pass since these pass write depth value, 
			// and this would make the hair composition fails in this cases.
			if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucent)
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, HairRendering, "HairRendering");
				RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);

				RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
			}

			if (bShouldRenderDistortion)
			{
				RenderDistortion(GraphBuilder, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
			}

			if (bShouldRenderVelocities && CVarTranslucencyVelocity.GetValueOnRenderThread() != 0)
			{
				const bool bRecreateSceneTextures = !HasBeenProduced(SceneTextures.Velocity);

				RenderVelocities(GraphBuilder, Views, SceneTextures, EVelocityPass::Translucent, false);

				if (bRecreateSceneTextures)
				{
					// Rebuild scene textures to include newly allocated velocity.
					SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
				}
			}
		}
		else if (GetHairStrandsComposition() == EHairStrandsCompositionType::AfterTranslucent)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, HairRendering, "HairRendering");
			RDG_GPU_STAT_SCOPE(GraphBuilder, HairRendering);

			RenderHairComposition(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures.Depth.Target, SceneTextures.Velocity, TranslucencyResourceMap);
		}

#if !UE_BUILD_SHIPPING
		if (CVarForceBlackVelocityBuffer.GetValueOnRenderThread())
		{
			SceneTextures.Velocity = SystemTextures.Black;

			// Rebuild the scene texture uniform buffer to include black.
			SceneTextures.UniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, &SceneTextures, FeatureLevel, SceneTextures.SetupMode);
		}
#endif

		{
			if (HairStrandsBookmarkParameters.HasInstances())
			{
				HairStrandsBookmarkParameters.SceneColorTexture = SceneTextures.Color.Target;
				HairStrandsBookmarkParameters.SceneDepthTexture = SceneTextures.Depth.Target;
				RenderHairStrandsDebugInfo(GraphBuilder, Scene, Views, HairStrandsBookmarkParameters);
			}
		}

		if (VirtualShadowMapArray.IsEnabled())
		{
			VirtualShadowMapArray.RenderDebugInfo(GraphBuilder, Views);
		}

		for (FViewInfo& View : Views)
		{
			ShadingEnergyConservation::Debug(GraphBuilder, View, SceneTextures);
		}

		if (ViewFamily.EngineShowFlags.VisualizeInstanceOcclusionQueries
			&& Scene->InstanceCullingOcclusionQueryRenderer)
		{
			for (FViewInfo& View : Views)
			{
				Scene->InstanceCullingOcclusionQueryRenderer->RenderDebug(GraphBuilder, Scene->GPUScene, View, SceneTextures);
			}
		}

		if (!bHasRayTracedOverlay && ViewFamily.EngineShowFlags.LightShafts)
		{
			SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderLightShaftBloom);
			RenderLightShaftBloom(GraphBuilder, SceneTextures, /* inout */ TranslucencyResourceMap);
		}

		{
			// Light shaft (rendered just above) can render in separate transluceny at low resolution according to r.SeparateTranslucencyScreenPercentage. 
			// So we can only upsample that buffer if required after the light shaft bloom pass.
			UpscaleTranslucencyIfNeeded(GraphBuilder, SceneTextures, TranslucencyViewsToRender, /* inout */ &TranslucencyResourceMap, TranslucencySharedDepthTexture);
			TranslucencyViewsToRender = ETranslucencyView::None;
		}

		FPathTracingResources PathTracingResources;

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			// Path tracer requires the full ray tracing pipeline support, as well as specialized extra shaders.
			// Most of the ray tracing debug visualizations also require the full pipeline, but some support inline mode.
			
			if (ViewFamily.EngineShowFlags.PathTracing 
				&& FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(Scene->GetShaderPlatform()))
			{
				for (const FViewInfo& View : Views)
				{
					RenderPathTracing(GraphBuilder, View, SceneTextures.UniformBuffer, SceneTextures.Color.Target, SceneTextures.Depth.Target,PathTracingResources);
				}
			}
			else if (ViewFamily.EngineShowFlags.RayTracingDebug)
			{
				for (const FViewInfo& View : Views)
				{
					FRayTracingPickingFeedback PickingFeedback = {};
					RenderRayTracingDebug(GraphBuilder, View, SceneTextures.Color.Target, PickingFeedback);

					OnGetOnScreenMessages.AddLambda([this, PickingFeedback](FScreenMessageWriter& ScreenMessageWriter)->void
						{
							RayTracingDisplayPicking(PickingFeedback, ScreenMessageWriter);
						});
				}
			}
		}
#endif
		if (bUseVirtualTexturing)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, VirtualTextureUpdate, "VirtualTextureUpdate");
			RDG_GPU_STAT_SCOPE(GraphBuilder, VirtualTextureUpdate);

			VirtualTextureFeedbackEnd(GraphBuilder);
		}

		RendererModule.RenderOverlayExtensions(GraphBuilder, Views, SceneTextures);

		if (ViewFamily.EngineShowFlags.PhysicsField && Scene->PhysicsField)
		{
			RenderPhysicsField(GraphBuilder, Views, Scene->PhysicsField, SceneTextures.Color.Target);
		}

		if (ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO && ShouldRenderDistanceFieldLighting())
		{
			// Use the skylight's max distance if there is one, to be consistent with DFAO shadowing on the skylight
			const float OcclusionMaxDistance = Scene->SkyLight && !Scene->SkyLight->bWantsStaticShadowing ? Scene->SkyLight->OcclusionMaxDistance : Scene->DefaultMaxDistanceFieldOcclusionDistance;
			TArray<FRDGTextureRef> DummyOutput;
			RenderDistanceFieldLighting(GraphBuilder, SceneTextures, FDistanceFieldAOParameters(OcclusionMaxDistance), DummyOutput, false, ViewFamily.EngineShowFlags.VisualizeDistanceFieldAO);
		}

		// Draw visualizations just before use to avoid target contamination
		if (ViewFamily.EngineShowFlags.VisualizeMeshDistanceFields || ViewFamily.EngineShowFlags.VisualizeGlobalDistanceField)
		{
			RenderMeshDistanceFieldVisualization(GraphBuilder, SceneTextures);
		}

		if (bRenderDeferredLighting)
		{
			RenderLumenMiscVisualizations(GraphBuilder, SceneTextures, LumenFrameTemporaries);
			RenderDiffuseIndirectAndAmbientOcclusion(
				GraphBuilder,
				SceneTextures,
				LumenFrameTemporaries,
				LightingChannelsTexture,
				/* bHasLumenLights = */ false,
				/* bCompositeRegularLumenOnly = */ false,
				/* bIsVisualizePass = */ true,
				AsyncLumenIndirectLightingOutputs);
		}

		if (ViewFamily.EngineShowFlags.StationaryLightOverlap)
		{
			RenderStationaryLightOverlap(GraphBuilder, SceneTextures, LightingChannelsTexture);
		}

		// Composite Heterogeneous Volumes
		if (!bHasRayTracedOverlay && ShouldRenderHeterogeneousVolumes(Scene) &&
			(GetHeterogeneousVolumesComposition() == EHeterogeneousVolumesCompositionType::AfterTranslucent))
		{
			CompositeHeterogeneousVolumes(GraphBuilder, SceneTextures);
		}

		if (bShouldVisualizeVolumetricCloud && !bHasRayTracedOverlay)
		{
			RenderVolumetricCloud(GraphBuilder, SceneTextures, false, true, false, HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, false, InstanceCullingManager);
			ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, false);
			ComposeVolumetricRenderTargetOverSceneForVisualization(GraphBuilder, Views, SceneTextures.Color.Target, SceneTextures);
			RenderVolumetricCloud(GraphBuilder, SceneTextures, true, false, false, HalfResolutionDepthCheckerboardMinMaxTexture, QuarterResolutionDepthMinMaxTexture, false, InstanceCullingManager);
		}

		if (!bHasRayTracedOverlay)
		{
			AddSparseVolumeTextureViewerRenderPass(GraphBuilder, *this, SceneTextures);
		}

		// Resolve the scene color for post processing.
		AddResolveSceneColorPass(GraphBuilder, Views, SceneTextures.Color);

		RendererModule.RenderPostResolvedSceneColorExtension(GraphBuilder, SceneTextures);

		CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, ViewFamilyDepthTexture, ViewFamily, Views);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			const FViewInfo& View = Views[ViewIndex];

			if (((View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::ScreenSpace && ScreenSpaceRayTracing::ShouldKeepBleedFreeSceneColor(View))
				|| GetViewPipelineState(View).DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen
				|| GetViewPipelineState(View).ReflectionsMethod == EReflectionsMethod::Lumen)
				&& !View.bStatePrevViewInfoIsReadOnly)
			{
				// Keep scene color and depth for next frame screen space ray tracing.
				FSceneViewState* ViewState = View.ViewState;
				GraphBuilder.QueueTextureExtraction(SceneTextures.Depth.Resolve, &ViewState->PrevFrameViewInfo.DepthBuffer);
				GraphBuilder.QueueTextureExtraction(SceneTextures.Color.Resolve, &ViewState->PrevFrameViewInfo.ScreenSpaceRayTracingInput);
			}
		}

		// Finish rendering for each view.
		if (ViewFamily.bResolveScene && ViewFamilyTexture)
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, Postprocessing, "PostProcessing");
			RDG_GPU_STAT_SCOPE(GraphBuilder, Postprocessing);
			SCOPED_NAMED_EVENT(PostProcessing, FColor::Emerald);

			FPostProcessingInputs PostProcessingInputs;
			PostProcessingInputs.ViewFamilyTexture = ViewFamilyTexture;
			PostProcessingInputs.ViewFamilyDepthTexture = ViewFamilyDepthTexture;
			PostProcessingInputs.CustomDepthTexture = SceneTextures.CustomDepth.Depth;
			PostProcessingInputs.ExposureIlluminance = ExposureIlluminance;
			PostProcessingInputs.SceneTextures = SceneTextures.UniformBuffer;
			PostProcessingInputs.bSeparateCustomStencil = SceneTextures.CustomDepth.bSeparateStencilBuffer;
			PostProcessingInputs.PathTracingResources = PathTracingResources;

			FRDGTextureRef InstancedEditorDepthTexture = nullptr; // Used to pass instanced stereo depth data from primary to secondary views

			GraphBuilder.FlushSetupQueue();

			if (ViewFamily.UseDebugViewPS())
			{
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					const Nanite::FRasterResults* NaniteResults = bNaniteEnabled ? &NaniteRasterResults[ViewIndex] : nullptr;
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);
					PostProcessingInputs.TranslucencyViewResourcesMap = FTranslucencyViewResourcesMap(TranslucencyResourceMap, ViewIndex);
					AddDebugViewPostProcessingPasses(GraphBuilder, View, GetSceneUniforms(), PostProcessingInputs, NaniteResults);
				}
			}
			else
			{
				for (int32 ViewExt = 0; ViewExt < ViewFamily.ViewExtensions.Num(); ++ViewExt)
				{
					for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
					{
						FViewInfo& View = Views[ViewIndex];
						RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
						PostProcessingInputs.TranslucencyViewResourcesMap = FTranslucencyViewResourcesMap(TranslucencyResourceMap, ViewIndex);
						ViewFamily.ViewExtensions[ViewExt]->PrePostProcessPass_RenderThread(GraphBuilder, View, PostProcessingInputs);
					}
				}
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					const int32 NaniteResultsIndex = View.bIsInstancedStereoEnabled ? View.PrimaryViewIndex : ViewIndex;
					const Nanite::FRasterResults* NaniteResults = bNaniteEnabled ? &NaniteRasterResults[NaniteResultsIndex] : nullptr;
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

					PostProcessingInputs.TranslucencyViewResourcesMap = FTranslucencyViewResourcesMap(TranslucencyResourceMap, ViewIndex);

					if (IsPostProcessVisualizeCalibrationMaterialEnabled(View))
					{
						const UMaterialInterface* DebugMaterialInterface = GetPostProcessVisualizeCalibrationMaterialInterface(View);
						check(DebugMaterialInterface);

						AddVisualizeCalibrationMaterialPostProcessingPasses(GraphBuilder, View, PostProcessingInputs, DebugMaterialInterface);
					}
					else
					{
						const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);
						const bool bAnyLumenActive = ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen || ViewPipelineState.ReflectionsMethod == EReflectionsMethod::Lumen;

						FScreenPassTexture TSRFlickeringInput;
						if (ViewIndex < TSRFlickeringInputTextures.Num())
						{
							TSRFlickeringInput = TSRFlickeringInputTextures[ViewIndex];
						}

						AddPostProcessingPasses(
							GraphBuilder,
							View, ViewIndex,
							GetSceneUniforms(),
							bAnyLumenActive,
							ViewPipelineState.DiffuseIndirectMethod,
							ViewPipelineState.ReflectionsMethod,
							PostProcessingInputs,
							NaniteResults,
							InstanceCullingManager,
							&VirtualShadowMapArray,
							LumenFrameTemporaries,
							SceneWithoutWaterTextures,
							TSRFlickeringInput,
							InstancedEditorDepthTexture);
					}
				}
			}
		}

		// After AddPostProcessingPasses in case of Lumen Visualizations writing to feedback
		FinishGatheringLumenSurfaceCacheFeedback(GraphBuilder, Views[0], LumenFrameTemporaries);

		if (ViewFamily.bResolveScene && ViewFamilyTexture)
		{
			GVRSImageManager.DrawDebugPreview(GraphBuilder, ViewFamily, ViewFamilyTexture);
		}

		GEngine->GetPostRenderDelegateEx().Broadcast(GraphBuilder);
		GetSceneExtensionsRenderers().PostRender(GraphBuilder);
	}

#if WITH_MGPU
	if (ViewFamily.bMultiGPUForkAndJoin)
	{
		DoCrossGPUTransfers(GraphBuilder, ViewFamilyTexture, Views, CrossGPUTransferFencesDefer.Num() > 0, RenderTargetGPUMask, CrossGPUTransferDeferred.GetReference());
	}
	FlushCrossGPUTransfers(GraphBuilder);
#endif

	{
		SCOPE_CYCLE_COUNTER(STAT_FDeferredShadingSceneRenderer_RenderFinish);

		RDG_EVENT_SCOPE_STAT(GraphBuilder, FrameRenderFinish, "FrameRenderFinish");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FrameRenderFinish);

		OnRenderFinish(GraphBuilder, ViewFamilyTexture);
		GraphBuilder.AddDispatchHint();
		GraphBuilder.FlushSetupQueue();
	}

	QueueSceneTextureExtractions(GraphBuilder, SceneTextures);

	::Substrate::PostRender(*Scene);
	HairStrands::PostRender(*Scene);
	HeterogeneousVolumes::PostRender(*Scene, Views);

	// Release the view's previous frame histories so that their memory can be reused at the graph's execution.
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		Views[ViewIndex].PrevViewInfo = FPreviousViewInfo();
	}

	if (NaniteBasePassVisibility.Visibility)
	{
		NaniteBasePassVisibility.Visibility->FinishVisibilityFrame();
		NaniteBasePassVisibility.Visibility = nullptr;
	}

	if (Scene->InstanceCullingOcclusionQueryRenderer)
	{
		Scene->InstanceCullingOcclusionQueryRenderer->EndFrame(GraphBuilder);
	}
}

#if RHI_RAYTRACING

bool AnyRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View)
{
	if (!IsRayTracingEnabled(View.GetShaderPlatform()) || Scene == nullptr)
	{
		return false;
	}

	// Path tracer, ray tracing visualization debug modes, and sky light ray tracing force ray tracing on, regardless of what the view says
	if (HasRayTracedOverlay(*View.Family)
		|| ShouldRenderRayTracingSkyLight(Scene->SkyLight, View.GetShaderPlatform()))
	{
		return true;
	}

	if (!View.IsRayTracingAllowedForView())
	{
		return false;
	}

	return ShouldRenderRayTracingAmbientOcclusion(View)
		|| ShouldRenderRayTracingTranslucency(View)
		|| ShouldRenderRayTracingShadows(*View.Family)
		|| Scene->bHasLightsWithRayTracedShadows
		|| ShouldRenderPluginRayTracingGlobalIllumination(View)
        || Lumen::AnyLumenHardwareRayTracingPassEnabled(Scene, View)
		|| MegaLights::UseHardwareRayTracing(*View.Family);
}

static bool ShouldRenderRayTracingEffectInternal(bool bEffectEnabled, ERayTracingPipelineCompatibilityFlags CompatibilityFlags)
{
	const bool bAllowPipeline = GRHISupportsRayTracingShaders && 
								CVarRayTracingAllowPipeline.GetValueOnRenderThread() &&
								EnumHasAnyFlags(CompatibilityFlags, ERayTracingPipelineCompatibilityFlags::FullPipeline);

	const bool bAllowInline = GRHISupportsInlineRayTracing && 
							  CVarRayTracingAllowInline.GetValueOnRenderThread() &&
							  EnumHasAnyFlags(CompatibilityFlags, ERayTracingPipelineCompatibilityFlags::Inline);

	// Disable the effect if current machine does not support the full ray tracing pipeline and the effect can't fall back to inline mode or vice versa.
	if (!bAllowPipeline && !bAllowInline)
	{
		return false;
	}

	const int32 OverrideMode = CVarForceAllRayTracingEffects.GetValueOnRenderThread();

	if (OverrideMode >= 0)
	{
		return OverrideMode > 0;
	}
	else
	{
		return bEffectEnabled;
	}
}

bool ShouldRenderRayTracingEffect(bool bEffectEnabled, ERayTracingPipelineCompatibilityFlags CompatibilityFlags, const FSceneView& View)
{
	if (!IsRayTracingEnabled(View.GetShaderPlatform()) || !View.IsRayTracingAllowedForView())
	{
		return false;
	}

	return ShouldRenderRayTracingEffectInternal(bEffectEnabled, CompatibilityFlags);
}

bool ShouldRenderRayTracingEffect(bool bEffectEnabled, ERayTracingPipelineCompatibilityFlags CompatibilityFlags, const FSceneViewFamily& ViewFamily)
{
	// TODO:  Should this check if ALL views have ray tracing?  ANY views have ray tracing?  Assert that all are the same?  All or any depending
	// on the specific feature or use case?  In practice, current examples (split screen or scene captures) will have ray tracing set the same
	// for all views, so we'll just check the first view of given a family, but having it be a separate function lets us reconsider that approach
	// in the future.
	return ShouldRenderRayTracingEffect(bEffectEnabled, CompatibilityFlags, *ViewFamily.Views[0]);
}

// Most ray tracing effects can be enabled or disabled per view, but the ray tracing sky light effect specifically requires base pass shaders
// in the FScene to be configured differently, and thus can't work if ray tracing is disabled.  There is logic in FScene::Update where
// bCachedShouldRenderSkylightInBasePass is updated based on the result of ShouldRenderSkylightInBasePass(), which is affected by whether sky light
// ray tracing is enabled.  When this value changes, bScenesPrimitivesNeedStaticMeshElementUpdate is set to true, forcing a rebuild of all static mesh
// elements in the scene.  This can't be done per frame (never mind per view), which would be required to allow this setting to vary, at least with
// the current implementation.  Sky light ray tracing is often used for cinematic capture, and not in games, so hopefully this isn't a big limitation.
// 
// This forces ray tracing on, but other ray tracing features are still disabled.  This is its own function to allow ShouldRenderRayTracingEffectInternal
// to be kept private, as all other effects should provide a view or view family, to allow IsRayTracingAllowedForView to be tested.
bool ShouldRenderRayTracingSkyLightEffect()
{
	return ShouldRenderRayTracingEffectInternal(true, ERayTracingPipelineCompatibilityFlags::FullPipeline);
}

bool HasRaytracingDebugViewModeRaytracedOverlay(const FSceneViewFamily& ViewFamily);
bool HasRayTracedOverlay(const FSceneViewFamily& ViewFamily)
{
	// Return true if a full screen ray tracing pass will be displayed on top of the raster pass
	// This can be used to skip certain calculations
	return
		ViewFamily.EngineShowFlags.PathTracing ||
		(ViewFamily.EngineShowFlags.RayTracingDebug && HasRaytracingDebugViewModeRaytracedOverlay(ViewFamily));
}

void FDeferredShadingSceneRenderer::InitializeRayTracingFlags_RenderThread()
{
	// The result of this call is used by AnyRayTracingPassEnabled to decide if we have any RT shadows enabled
	Scene->UpdateRayTracedLights(ViewFamily);

	// This function may be called twice -- once in CreateSceneRenderers and again in Render.  We deliberately skip the logic
	// if the flag is already set, because CreateSceneRenderers fills in the correct value for "bShouldUpdateRayTracingScene"
	// in that case, and we don't want to overwrite it.
	if (!bAnyRayTracingPassEnabled && !ViewFamily.EngineShowFlags.HitProxies)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			bool bHasRayTracing = AnyRayTracingPassEnabled(Scene, Views[ViewIndex]);

			Views[ViewIndex].bHasAnyRayTracingPass = bHasRayTracing;

			bAnyRayTracingPassEnabled |= bHasRayTracing;
		}

		bShouldUpdateRayTracingScene = bAnyRayTracingPassEnabled;
	}
}
#endif // RHI_RAYTRACING
