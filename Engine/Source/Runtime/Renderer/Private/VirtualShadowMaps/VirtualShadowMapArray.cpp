// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualShadowMapArray.h"
#include "VirtualShadowMapShaders.h"
#include "BasePassRendering.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Components/LightComponent.h"
#include "GPUMessaging.h"
#include "HairStrands/HairStrandsData.h"
#include "InstanceCulling/InstanceCullingMergedContext.h"
#include "Nanite/Nanite.h"
#include "RendererModule.h"
#include "Rendering/NaniteResources.h"
#include "ScenePrivate.h"
#include "SceneTextureReductions.h"
#include "ScreenPass.h"
#include "ShaderPrint.h"
#include "ShaderPrintParameters.h"
#include "VirtualShadowMapDefinitions.h"
#include "VirtualShadowMapCacheManager.h"
#include "VirtualShadowMapClipmap.h"
#include "VirtualShadowMapVisualizationData.h"
#include "SingleLayerWaterRendering.h"
#include "RenderUtils.h"
#include "SceneCulling/SceneCullingRenderer.h"
#include "Froxel/Froxel.h"

#define DEBUG_ALLOW_STATIC_SEPARATE_WITHOUT_CACHING 0

IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(VirtualShadowMapUbSlot);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVirtualShadowMapUniformParameters, "VirtualShadowMap", VirtualShadowMapUbSlot);

// Disabled by default: use either console command "CsvCategory VSM" or command line argument "-CsvCategories=VSM[,...]" to enable.
CSV_DEFINE_CATEGORY(VSM, false);

DECLARE_DWORD_COUNTER_STAT(TEXT("VSM Nanite Views (Primary)"), STAT_VSMNaniteViewsPrimary, STATGROUP_ShadowRendering);
DECLARE_DWORD_COUNTER_STAT(TEXT("VSM Single Page Count"), STAT_VSMSinglePageCount, STATGROUP_ShadowRendering);
DECLARE_DWORD_COUNTER_STAT(TEXT("VSM Full Count"), STAT_VSMFullCount, STATGROUP_ShadowRendering);

extern int32 GForceInvalidateDirectionalVSM;
extern int32 GVSMMaxPageAgeSinceLastRequest;
extern TAutoConsoleVariable<float> CVarNaniteMaxPixelsPerEdge;
extern TAutoConsoleVariable<float> CVarNaniteMinPixelsPerEdgeHW;
extern float GMinScreenRadiusForShadowCaster;

int32 GVSMShowLightDrawEvents = 0;
FAutoConsoleVariableRef CVarVSMShowLightDrawEvents(
	TEXT("r.Shadow.Virtual.ShowLightDrawEvents"),
	GVSMShowLightDrawEvents,
	TEXT("Enable Virtual Shadow Maps per-light draw events - may affect performance especially when there are many small lights in the scene."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkPagesUseFroxels(
	TEXT("r.Shadow.Virtual.MarkPagesUsingFroxels"),
	0,
	TEXT("Experimental: If enabled the virtual shadow map pages are marked using froxels that are generated during HZB build.\n")
	TEXT("  Higher throughput as it is not bandwidth limited. Is approximate as it only marks the center of each froxel representing 8x8 pixels."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDebugDrawFroxels(
	TEXT("r.Shadow.Virtual.DebugDrawFroxels"),
	0,
	TEXT("Render the froxels using shaderprint (which needs to be enabled) r.ShaderPrint.MaxLine also needs to be set to a high value as this produces many lines."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarDebugDrawFroxelRange(
	TEXT("r.Shadow.Virtual.DebugDrawFroxelRange"),
	20.0f,
	TEXT("Range in froxel tiles from the mouse cursor which to draw debug froxels in."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarEnableVirtualShadowMaps(
	TEXT("r.Shadow.Virtual.Enable"),
	0,
	TEXT("Enable Virtual Shadow Maps. Renders geometry into virtualized shadow depth maps for shadowing.\n")
	TEXT("Provides high - quality shadows for next - gen projects with simplified setup.High efficiency culling when used with Nanite."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		// Needed because the depth state changes with method (so cached draw commands must be re-created) see SetStateForShadowDepth
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMaxPhysicalPages(
	TEXT("r.Shadow.Virtual.MaxPhysicalPages"),
	2048,
	TEXT("Maximum number of physical pages in the pool.\n")
	TEXT("More space for pages means more memory usage, but allows for higher resolution shadows.\n")
	TEXT("Ideally this value is large enough to fit enough pages for all the lights in the scene, but not too large to waste memory.\n")
	TEXT("Enable 'ShowStats' to see how many pages are allocated in the pool right now.\n")
	TEXT("For more page pool control, see the 'ResolutionLodBias*', 'DynamicRes.*' and 'Cache.StaticSeparate' cvars."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarCacheStaticSeparate(
	TEXT("r.Shadow.Virtual.Cache.StaticSeparate"),
	1,
	TEXT("When enabled, caches static objects in separate pages from dynamic objects.\n")
	TEXT("This can improve performance in largely static scenes, but doubles the memory cost of the physical page pool."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarBuildDynamicHZB(
	TEXT("r.Shadow.Virtual.DynamicHZB"),
	0,
	TEXT("When enabled, a separate HZB is built for dynamic cached pages.\n")
	TEXT("This can improve performance in cached scenes with a lot of dynamic overdraw, e.g. a forest with a static sun light.\n")
	TEXT("Constructing separate HZB doubles the memory cost of the HZB (1/4 of the page pool) and incurs some cost for building the second HZB."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarShowStats(
	TEXT("r.Shadow.Virtual.ShowStats"),
	0,
	TEXT("Show VSM statistics."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarPageDilationBorderSizeDirectional(
	TEXT("r.Shadow.Virtual.PageDilationBorderSizeDirectional"),
	0.05f,
	TEXT("If a screen pixel falls within this fraction of a page border for directional lights, the adacent page will also be mapped.")
	TEXT("Higher values can reduce page misses at screen edges or disocclusions, but increase total page counts."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarPageDilationBorderSizeLocal(
	TEXT("r.Shadow.Virtual.PageDilationBorderSizeLocal"),
	0.05f,
	TEXT("If a screen pixel falls within this fraction of a page border for local lights, the adacent page will also be mapped.")
	TEXT("Higher values can reduce page misses at screen edges or disocclusions, but increase total page counts."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkPixelPages(
	TEXT("r.Shadow.Virtual.MarkPixelPages"),
	1,
	TEXT("Marks pages in virtual shadow maps based on depth buffer pixels. Ability to disable is primarily for profiling and debugging."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkPixelPagesMipModeLocal(
	TEXT("r.Shadow.Virtual.MarkPixelPagesMipModeLocal"),
	0,
	TEXT("When enabled, this uses a subset of mips to reduce instance duplication in VSMs. Will result in better performance but a harsher falloff on mip transitions.\n")
	TEXT(" 0 - Disabled: Use all 8 mips\n")
	TEXT(" 1 - Quality Mode: Use 4 higher res mips (16k, 4k, 1k, 256)\n")
	TEXT(" 2 - Performance Mode: Use 4 lower res mips (8k, 2k, 512, 128)\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability
);

TAutoConsoleVariable<int32> CVarMarkCoarsePagesDirectional(
	TEXT("r.Shadow.Virtual.MarkCoarsePagesDirectional"),
	1,
	TEXT("Marks coarse pages in directional light virtual shadow maps so that low resolution data is available everywhere.")
	TEXT("Ability to disable is primarily for profiling and debugging."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarMarkCoarsePagesLocal(
	TEXT("r.Shadow.Virtual.MarkCoarsePagesLocal"),
	1,
	TEXT("Marks coarse pages in local light virtual shadow maps so that low resolution data is available everywhere.")
	TEXT("Ability to disable is primarily for profiling and debugging."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarCoarsePagesIncludeNonNanite(
	TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages"),
	1,
	TEXT("Include non-Nanite geometry in coarse pages.")
	TEXT("Rendering non-Nanite geometry into large coarse pages can be expensive; disabling this can be a significant performance win."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarNonNaniteCulledInstanceAllocationFactor(
	TEXT("r.Shadow.Virtual.NonNanite.CulledInstanceAllocationFactor"),
	1.0f,
	TEXT("Allocation size scale factor for the buffer used to store instances after culling.\n")
	TEXT("The total size accounts for the worst-case scenario in which all instances are emitted into every clip or mip level.\n")
	TEXT("This is far more than we'd expect in reasonable circumstances, so this scale factor is used to reduce memory pressure.\n")
	TEXT("The actual number cannot be known on the CPU as the culling emits an instance for each clip/mip level that is overlapped.\n")
	TEXT("Setting to 1.0 is fully conservative. Lowering this is likely to produce artifacts unless you're certain the buffer won't overflow."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarNonNaniteMaxCulledInstanceAllocationSize(
	TEXT("r.Shadow.Virtual.NonNanite.MaxCulledInstanceAllocationSize"),
	128 * 1024 * 1024,
	TEXT("Maximum number of instances that may be output from the culling pass into all VSM mip/clip levels. At 12 byte per instance reference this represents a 1.5GB clamp."),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<int32> CVarShowClipmapStats(
	TEXT("r.Shadow.Virtual.ShowClipmapStats"),
	-1,
	TEXT("Set to the number of clipmap you want to show stats for (-1 == off)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCullBackfacingPixels(
	TEXT("r.Shadow.Virtual.CullBackfacingPixels"),
	1,
	TEXT("When enabled does not generate shadow data for pixels that are backfacing to the light."),
	ECVF_RenderThreadSafe
);

int32 GEnableNonNaniteVSM = 1;
FAutoConsoleVariableRef CVarEnableNonNaniteVSM(
	TEXT("r.Shadow.Virtual.NonNaniteVSM"),
	GEnableNonNaniteVSM,
	TEXT("Enable support for non-nanite Virtual Shadow Maps.")
	TEXT("Read-only and to be set in a config file (requires restart)."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static TAutoConsoleVariable<int32> CVarNonNaniteVsmUseHzb(
	TEXT("r.Shadow.Virtual.NonNanite.UseHZB"),
	2,
	TEXT("Cull Non-Nanite instances using HZB.\n")
	TEXT("  Set to 0 to disable.\n")
	TEXT("  Set to 1 to use HZB from previous frame. Can incorrectly cull in some cases due to outdated data.\n")
	TEXT("  Set to 2 to use two-pass Nanite culling with HZB from the current frame."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarVirtualShadowOnePassProjectionMaxLights(
	TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel"),
	16,
	TEXT("Maximum lights per pixel that get full filtering when using one pass projection and clustered shading.")
	TEXT("Generally set to 8 (32bpp), 16 (64bpp) or 32 (128bpp). Lower values require less transient VRAM during the lighting pass."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDoNonNaniteBatching(
	TEXT("r.Shadow.Virtual.NonNanite.Batch"),
	1,
	TEXT("."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarNonNaniteUseRadiusThreshold(
	TEXT("r.Shadow.Virtual.NonNanite.UseRadiusThreshold"),
	1,
	TEXT("If enabled (default) the r.Shadow.RadiusThreshold cvar is also used for uncached virtual shadow maps to cull small non-nanite instances."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarCoarsePagePixelThresholdDynamic(
	TEXT("r.Shadow.Virtual.CoarsePagePixelThresholdDynamic"),
	16.0f,
	TEXT("If a dynamic (non-nanite) instance has a smaller estimated pixel footprint than this value, it should not be drawn into a coarse page. Higher values cull away more instances."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarCoarsePagePixelThresholdStatic(
	TEXT("r.Shadow.Virtual.CoarsePagePixelThresholdStatic"),
	1.0f,
	TEXT("If a static (non-nanite) instance has a smaller estimated pixel footprint than this value, it should not be drawn into a coarse page. Higher values cull away more instances.\n")
	TEXT("This value is typically lower than the non-static one because the static pages have better caching."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarCoarsePagePixelThresholdDynamicNanite(
	TEXT("r.Shadow.Virtual.CoarsePagePixelThresholdDynamicNanite"),
	4.0f,
	TEXT("If a dynamic Nanite instance has a smaller estimated pixel footprint than this value, it should not be drawn into a coarse page. Higher values cull away more instances.\n")
	TEXT("This value is typically lower than the non-Nanite one because Nanite has lower overhead for drawing small objects."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarCacheAllocateViaLRU(
	TEXT("r.Shadow.Virtual.Cache.AllocateViaLRU"),
	1,
	TEXT("Prioritizes keeping more recently requested cached physical pages when allocating for new requests."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarClipmapGreedyLevelSelection(
	TEXT("r.Shadow.Virtual.Clipmap.GreedyLevelSelection"),
	0,
	TEXT("When enabled, allows greedily sampling more detailed clipmap levels if they happen to be mapped.\n")
	TEXT("This can increase shadow quality from certain viewing angles, but makes the clipmap boundry less stable which can exacerbate visual artifacts at low shadow resolutions."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

#if !UE_BUILD_SHIPPING
bool GDumpVSMLightNames = false;
void DumpVSMLightNames()
{
	ENQUEUE_RENDER_COMMAND(DumpVSMLightNames)(
		[](FRHICommandList& RHICmdList)
		{
			GDumpVSMLightNames = true;
		});
}

FAutoConsoleCommand CmdDumpVSMLightNames(
	TEXT("r.Shadow.Virtual.Visualize.DumpLightNames"),
	TEXT("Dump light names with virtual shadow maps (for developer use in non-shipping builds)"),
	FConsoleCommandDelegate::CreateStatic(DumpVSMLightNames)
);

UPTRINT GVirtualShadowMapLastSelectedVisualizeLightId = 0;
UPTRINT GVirtualShadowMapVisualizeLightId = 0;
bool GVirtualShadowMapVisualizeByLightId = false;

FString GVirtualShadowMapVisualizeLightName;
FAutoConsoleVariableRef CVarVisualizeLightName(
	TEXT("r.Shadow.Virtual.Visualize.LightName"),
	GVirtualShadowMapVisualizeLightName,
	TEXT("Sets the name of a specific light to visualize (for developer use in non-shipping builds)"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		GVirtualShadowMapVisualizeByLightId = false;
	}),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarVisualizeLayout(
	TEXT("r.Shadow.Virtual.Visualize.Layout"),
	0,
	TEXT("Overlay layout when virtual shadow map visualization is enabled:\n")
	TEXT("  0: Full screen\n")
	TEXT("  1: Thumbnail\n")
	TEXT("  2: Split screen"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDebugSkipMergePhysical(
	TEXT("r.Shadow.Virtual.DebugSkipMergePhysical"),
	0,
	TEXT("Skip the merging of the static VSM cache into the dynamic one. This will create obvious visual artifacts when disabled."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarDebugSkipDynamicPageInvalidation(
	TEXT("r.Shadow.Virtual.Cache.DebugSkipDynamicPageInvalidation"),
	0,
	TEXT("Skip invalidation of cached pages when geometry moves for debugging purposes. This will create obvious visual artifacts when disabled."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarNumPageAreaDiagSlots(
	TEXT("r.Shadow.Virtual.NonNanite.NumPageAreaDiagSlots"),
	0,
	TEXT("Number of slots in diagnostics to report non-nanite instances with the largest page area coverage, < 0 uses the max number allowed, 0 disables."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarLargeInstancePageAreaThreshold(
	TEXT("r.Shadow.Virtual.NonNanite.LargeInstancePageAreaThreshold"),
	-1,
	TEXT("How large area is considered a 'large' footprint, summed over all overlapped levels, if set to -1 uses the physical page pool size / 8.\n")
	TEXT("Used as a threshold when storing page area coverage stats for diagnostics."),
	ECVF_RenderThreadSafe
);
#endif // !UE_BUILD_SHIPPING

static TAutoConsoleVariable<int32> CVarShadowsVirtualUseHZB(
	TEXT("r.Shadow.Virtual.UseHZB"),
	2,
	TEXT("Enables HZB for (Nanite) Virtual Shadow Maps - Non-Nanite unfortunately has a separate flag with different semantics: r.Shadow.Virtual.NonNanite.UseHZB.\n")
	TEXT(" 0 - No HZB occlusion culling\n")
	TEXT(" 1 - Approximate Single-pass HZB occlusion culling (using previous frame HZB)\n")
	TEXT(" 2 - Two-pass occlusion culling (default)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarShadowsVirtualForceFullHZBUpdate(
	TEXT("r.Shadow.Virtual.ForceFullHZBUpdate"),
	0,
	TEXT("Forces full HZB update every frame rather than just dirty pages.\n"),
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<int32> CVarVirtualShadowSinglePassBatched(
	TEXT("r.Shadow.Virtual.NonNanite.SinglePassBatched"),
	1,
	TEXT("."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVirtualShadowMapPageMarkingPixelStrideX(
	TEXT("r.Shadow.Virtual.PageMarkingPixelStrideX"),
	2,
	TEXT("During page marking, instead of testing every screen pixel, test every Nth pixel.\n")
	TEXT("Page marking from screen pixels is used to determine which VSM pages are seen from the camera and need to be rendered.\n")
	TEXT("Increasing this value reduces page-marking costs, but could introduce artifacts due to missing pages.\n")
	TEXT("With sufficiently low values, it is likely a neighbouring pixel will mark the required page anyway."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVirtualShadowMapPageMarkingPixelStrideY(
	TEXT("r.Shadow.Virtual.PageMarkingPixelStrideY"),
	2,
	TEXT("Same as PageMarkingPixelStrideX, but on the vertical axis of the screen."),
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<float> CVarScreenRayLength(
	TEXT("r.Shadow.Virtual.ScreenRayLength"),
	0.015f,
	TEXT("Length of the screen space shadow trace away from receiver surface (smart shadow bias) before the VSM / SMRT lookup."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarNormalBias(
	TEXT("r.Shadow.Virtual.NormalBias"),
	0.5f,
	TEXT("Receiver offset along surface normal for shadow lookup. Scaled by distance to camera.")
	TEXT("Higher values avoid artifacts on surfaces nearly parallel to the light, but also visibility offset shadows and increase the chance of hitting unmapped pages."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTAdaptiveRayCount(
	TEXT("r.Shadow.Virtual.SMRT.AdaptiveRayCount"),
	1,
	TEXT("Shoot fewer rays in fully shadowed and unshadowed regions. Currently only supported with OnePassProjection. "),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTRayCountLocal(
	TEXT("r.Shadow.Virtual.SMRT.RayCountLocal"),
	7,
	TEXT("Ray count for shadow map tracing of local lights. 0 = disabled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTSamplesPerRayLocal(
	TEXT("r.Shadow.Virtual.SMRT.SamplesPerRayLocal"),
	8,
	TEXT("Shadow map samples per ray for local lights"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTMaxRayAngleFromLight(
	TEXT("r.Shadow.Virtual.SMRT.MaxRayAngleFromLight"),
	0.03f,
	TEXT("Max angle (in radians) a ray is allowed to span from the light's perspective for local lights.")
	TEXT("Smaller angles limit the screen space size of shadow penumbra. ")
	TEXT("Larger angles lead to more noise. "),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarSMRTExtrapolateMaxSlopeLocal(
	TEXT("r.Shadow.Virtual.SMRT.ExtrapolateMaxSlopeLocal"),
	0.05f,
	TEXT("Maximum depth slope when extrapolating behind occluders for local lights.\n")
	TEXT("Higher values allow softer penumbra edges but can introduce light leaks behind second occluders.\n")
	TEXT("Setting to 0 will disable slope extrapolation slightly improving projection performance, at the cost of reduced penumbra quality."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTTexelDitherScaleLocal(
	TEXT("r.Shadow.Virtual.SMRT.TexelDitherScaleLocal"),
	2.0f,
	TEXT("Applies a dither to the shadow map ray casts for local lights to help hide aliasing due to insufficient shadow resolution.\n")
	TEXT("Setting this too high can cause shadows light leaks near occluders."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarSMRTMaxSlopeBiasLocal(
	TEXT("r.Shadow.Virtual.SMRT.MaxSlopeBiasLocal"),
	50.0f,
	TEXT("Maximum depth slope. Low values produce artifacts if shadow resolution is insufficient. High values can worsen light leaks near occluders and sparkly pixels in shadowed areas."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTRayCountDirectional(
	TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"),
	7,
	TEXT("Ray count for shadow map tracing of directional lights. 0 = disabled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTSamplesPerRayDirectional(
	TEXT("r.Shadow.Virtual.SMRT.SamplesPerRayDirectional"),
	8,
	TEXT("Shadow map samples per ray for directional lights"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarSMRTExtrapolateMaxSlopeDirectional(
	TEXT("r.Shadow.Virtual.SMRT.ExtrapolateMaxSlopeDirectional"),
	5.0f,
	TEXT("Maximum depth slope when extrapolating behind occluders for directional lights.\n")
	TEXT("Higher values allow softer penumbra edges but can introduce light leaks behind second occluders.\n")
	TEXT("Setting to 0 will disable slope extrapolation slightly improving projection performance, at the cost of reduced penumbra quality."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTTexelDitherScaleDirectional(
	TEXT("r.Shadow.Virtual.SMRT.TexelDitherScaleDirectional"),
	2.0f,
	TEXT("Applies a dither to the shadow map ray casts for directional lights to help hide aliasing due to insufficient shadow resolution.\n")
	TEXT("Setting this too high can cause shadows light leaks near occluders."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarSMRTRayLengthScaleDirectional(
	TEXT("r.Shadow.Virtual.SMRT.RayLengthScaleDirectional"),
	1.5f,
	TEXT("Length of ray to shoot for directional lights, scaled by distance to camera.")
	TEXT("Shorter rays limit the screen space size of shadow penumbra. ")
	TEXT("Longer rays require more samples to avoid shadows disconnecting from contact points. "),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarSMRTRayCountHair(
	TEXT("r.Shadow.Virtual.SMRT.SamplesPerRayHair"),
	1,
	TEXT("Shadow map samples per ray for hair"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace Nanite
{
	extern bool IsStatFilterActive(const FString& FilterName);
}

bool IsVSMTranslucentHighQualityEnabled();
bool IsLumenFrontLayerHistoryValid(const FViewInfo& View);
extern bool LightGridUses16BitBuffers(EShaderPlatform Platform);

bool DoesVSMWantFroxels(EShaderPlatform ShaderPlatform)
{
	return UseVirtualShadowMaps(ShaderPlatform)
		&& CVarMarkPagesUseFroxels.GetValueOnRenderThread() != 0
		// fall back to per-pixel marking if the front layer translucency path is enabled as it doesn't generate froxels.
		&& !IsVSMTranslucentHighQualityEnabled();
}

FMatrix CalcTranslatedWorldToShadowUVMatrix(
	const FMatrix& TranslatedWorldToShadowView,
	const FMatrix& ViewToClip)
{
	FMatrix TranslatedWorldToShadowClip = TranslatedWorldToShadowView * ViewToClip;
	FMatrix ScaleAndBiasToSmUV = FScaleMatrix(FVector(0.5f, -0.5f, 1.0f)) * FTranslationMatrix(FVector(0.5f, 0.5f, 0.0f));
	FMatrix TranslatedWorldToShadowUv = TranslatedWorldToShadowClip * ScaleAndBiasToSmUV;
	return TranslatedWorldToShadowUv;
}

FMatrix CalcTranslatedWorldToShadowUVNormalMatrix(
	const FMatrix& TranslatedWorldToShadowView,
	const FMatrix& ViewToClip)
{
	return CalcTranslatedWorldToShadowUVMatrix(TranslatedWorldToShadowView, ViewToClip).GetTransposed().Inverse();
}

static float GetNormalBiasForShader()
{
	return CVarNormalBias.GetValueOnRenderThread() / 1000.0f;
}

FVirtualShadowMapArray::FVirtualShadowMapArray(FScene& InScene) 
	: Scene(InScene)
{
}

void FVirtualShadowMapArray::UpdateNextData(int32 PrevVirtualShadowMapId, int32 NextVirtualShadowMapId, FInt32Point PageOffset)
{
	// Fill in any slots with empty mappings
	FNextVirtualShadowMapData EmptyData;
	EmptyData.NextVirtualShadowMapId = INDEX_NONE;
	EmptyData.PageAddressOffset = FIntVector2(0, 0);

	// TODO: Some ways to optimize this
	// Can't use SetNum because we need the empty item initializer which doesn't fit nicely with our shared HLSL definition right now
	NextData.Reserve(PrevVirtualShadowMapId);
	while (PrevVirtualShadowMapId >= NextData.Num())
	{
		NextData.Add(EmptyData);
	}

	NextData[PrevVirtualShadowMapId].NextVirtualShadowMapId = NextVirtualShadowMapId;
	NextData[PrevVirtualShadowMapId].PageAddressOffset = FIntVector2(PageOffset.X, PageOffset.Y);
}

static FVirtualShadowMapPerViewParameters MakeEmptyVirtualShadowMapPerViewParameters(FRDGBuilder& GraphBuilder)
{
	FVirtualShadowMapPerViewParameters PerViewData;
	PerViewData.MaxLightGridEntryIndex = 0u;
	PerViewData.NumCulledLightsGrid = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32), 0u));
	PerViewData.LightGridData = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32), 0u));

	return PerViewData;
}

void FVirtualShadowMapArray::Initialize(
	FRDGBuilder& GraphBuilder,
	FVirtualShadowMapArrayCacheManager* InCacheManager,
	bool bInEnabled,
	const FEngineShowFlags& EngineShowFlags)
{
	bInitialized = true;
	bEnabled = bInEnabled;
	CacheManager = InCacheManager;

	bCullBackfacingPixels = CVarCullBackfacingPixels.GetValueOnRenderThread() != 0;
	bUseHzbOcclusion = CVarShadowsVirtualUseHZB.GetValueOnRenderThread() != 0;
	bUseTwoPassHzbOcclusion = CVarShadowsVirtualUseHZB.GetValueOnRenderThread() == 2;
	bNonNaniteUseRadiusThreshold = CVarNonNaniteUseRadiusThreshold.GetValueOnRenderThread() != 0;
	UniformParameters.NumFullShadowMaps = 0;
	UniformParameters.NumSinglePageShadowMaps = 0;
	UniformParameters.NumShadowMapSlots = 0;
	UniformParameters.MaxPhysicalPages = 0;
	UniformParameters.StaticCachedArrayIndex = 0;
	UniformParameters.StaticHZBArrayIndex = 0;
	// NOTE: Most uniform values don't matter when VSM is disabled

	UniformParameters.bExcludeNonNaniteFromCoarsePages = !CVarCoarsePagesIncludeNonNanite.GetValueOnRenderThread();
	UniformParameters.CoarsePagePixelThresholdDynamic = CVarCoarsePagePixelThresholdDynamic.GetValueOnRenderThread();
	UniformParameters.CoarsePagePixelThresholdStatic = CVarCoarsePagePixelThresholdStatic.GetValueOnRenderThread();
	UniformParameters.CoarsePagePixelThresholdDynamicNanite = CVarCoarsePagePixelThresholdDynamicNanite.GetValueOnRenderThread();
	UniformParameters.bClipmapGreedyLevelSelection = CVarClipmapGreedyLevelSelection.GetValueOnRenderThread();

	UniformParameters.SceneFrameNumber = Scene.GetFrameNumberRenderThread();
	
	// Global SMRT settings so they can be shared between different passes that call into them
	UniformParameters.ScreenRayLength = CVarScreenRayLength.GetValueOnRenderThread();
	UniformParameters.NormalBias = GetNormalBiasForShader();

	UniformParameters.SMRTAdaptiveRayCount = CVarSMRTAdaptiveRayCount.GetValueOnRenderThread();

	UniformParameters.SMRTRayCountLocal = CVarSMRTRayCountLocal.GetValueOnRenderThread();
	UniformParameters.SMRTSamplesPerRayLocal = CVarSMRTSamplesPerRayLocal.GetValueOnRenderThread();
	UniformParameters.SMRTExtrapolateMaxSlopeLocal = CVarSMRTExtrapolateMaxSlopeLocal.GetValueOnRenderThread();
	UniformParameters.SMRTTexelDitherScaleLocal = CVarSMRTTexelDitherScaleLocal.GetValueOnRenderThread();
	UniformParameters.SMRTMaxSlopeBiasLocal = CVarSMRTMaxSlopeBiasLocal.GetValueOnRenderThread();
	UniformParameters.SMRTCotMaxRayAngleFromLight = 1.0f / FMath::Tan(CVarSMRTMaxRayAngleFromLight.GetValueOnRenderThread());
	
	UniformParameters.SMRTRayCountDirectional = CVarSMRTRayCountDirectional.GetValueOnRenderThread();
	UniformParameters.SMRTSamplesPerRayDirectional = CVarSMRTSamplesPerRayDirectional.GetValueOnRenderThread();
	UniformParameters.SMRTExtrapolateMaxSlopeDirectional = CVarSMRTExtrapolateMaxSlopeDirectional.GetValueOnRenderThread();
	UniformParameters.SMRTTexelDitherScaleDirectional = CVarSMRTTexelDitherScaleDirectional.GetValueOnRenderThread();
	UniformParameters.SMRTRayLengthScale = CVarSMRTRayLengthScaleDirectional.GetValueOnRenderThread();

	UniformParameters.SMRTHairRayCount = CVarSMRTRayCountHair.GetValueOnRenderThread();

	// Reference dummy data in the UB initially
	const uint32 DummyPageTableElement = 0xFFFFFFFF;
	UniformParameters.PageTable = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(DummyPageTableElement), DummyPageTableElement));
	UniformParameters.ProjectionData = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultByteAddressBuffer(GraphBuilder, sizeof(FVirtualShadowMapProjectionShaderData)));
	UniformParameters.PageFlags = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32)));
	UniformParameters.UncachedPageRectBounds = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FIntVector4)));
	UniformParameters.AllocatedPageRectBounds = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FIntVector4)));
	UniformParameters.PerViewData = MakeEmptyVirtualShadowMapPerViewParameters(GraphBuilder);
	UniformParameters.CachePrimitiveAsDynamic = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32)));

	if (bEnabled)
	{
		// Always reserve IDs for the single-page SMs.
		NumShadowMapSlots = VSM_MAX_SINGLE_PAGE_SHADOW_MAPS;

		// Fixed physical page pool width, we adjust the height to accomodate the requested maximum
		// NOTE: Row size in pages has to be POT since we use mask & shift in place of integer ops
		// NOTE: This assumes GetMax2DTextureDimension() is a power of two on supported platforms
		const uint32 PhysicalPagesX = FMath::DivideAndRoundDown(GetMax2DTextureDimension(), FVirtualShadowMap::PageSize);
		check(FMath::IsPowerOfTwo(PhysicalPagesX));
		const int32 MaxPhysicalPages = CVarMaxPhysicalPages.GetValueOnRenderThread();
		uint32 PhysicalPagesY = FMath::DivideAndRoundUp((uint32)FMath::Max(1, MaxPhysicalPages), PhysicalPagesX);	

		UniformParameters.MaxPhysicalPages = PhysicalPagesX * PhysicalPagesY;
				
		if (CVarCacheStaticSeparate.GetValueOnRenderThread() != 0)
		{
			#if !DEBUG_ALLOW_STATIC_SEPARATE_WITHOUT_CACHING
			if (CacheManager->IsCacheEnabled())
			#endif
			{
				// Only set up the dynamic separate HZB build allocation if 
				// 1. caching is enabled & we cache static separate (otherwise they are combined anyway)
				// 2. the cvar is enabled
				if (CVarBuildDynamicHZB.GetValueOnRenderThread() != 0)
				{
					UniformParameters.StaticHZBArrayIndex = 1;
				}

				// Enable separate static caching in the second texture array element
				UniformParameters.StaticCachedArrayIndex = 1;
			}
		}

		uint32 PhysicalX = PhysicalPagesX * FVirtualShadowMap::PageSize;
		uint32 PhysicalY = PhysicalPagesY * FVirtualShadowMap::PageSize;

		// TODO: Some sort of better fallback with warning?
		// All supported platforms support at least 16384 texture dimensions which translates to 16384 max pages with default 128x128 page size
		check(PhysicalX <= GetMax2DTextureDimension());
		check(PhysicalY <= GetMax2DTextureDimension());

		UniformParameters.PhysicalPageRowMask = (PhysicalPagesX - 1);
		UniformParameters.PhysicalPageRowShift = FMath::FloorLog2( PhysicalPagesX );
		UniformParameters.RecPhysicalPoolSize = FVector4f( 1.0f / PhysicalX, 1.0f / PhysicalY, 1.0f, 1.0f );
		UniformParameters.PhysicalPoolSize = FIntPoint( PhysicalX, PhysicalY );
		UniformParameters.PhysicalPoolSizePages = FIntPoint( PhysicalPagesX, PhysicalPagesY );

		UniformParameters.GlobalResolutionLodBias = CacheManager->GetGlobalResolutionLodBias();

		// TODO: Parameterize this in a useful way; potentially modify it automatically
		// when there are fewer lights in the scene and/or clustered shading settings differ.
		UniformParameters.PackedShadowMaskMaxLightCount = FMath::Min(CVarVirtualShadowOnePassProjectionMaxLights.GetValueOnRenderThread(), 32);

		// Set up nanite visualization if enabled. We use an extra array slice in the physical page pool for debug output
		// so need to set this up in advance.
		if (EngineShowFlags.VisualizeVirtualShadowMap)
		{
			bEnableVisualization = true;

			FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
			if (VisualizationData.GetActiveModeID() == VIRTUAL_SHADOW_MAP_VISUALIZE_NANITE_OVERDRAW)
			{
				bEnableNaniteVisualization = true;
			}
		}

		// If enabled, ensure we have a properly-sized physical page pool
		// We can do this here since the pool is independent of the number of shadow maps
		const int PoolArraySize = bEnableNaniteVisualization ? 3 : (ShouldCacheStaticSeparately() ? 2 : 1);
		CacheManager->SetPhysicalPoolSize(GraphBuilder, GetPhysicalPoolSize(), PoolArraySize, GetMaxPhysicalPages());
		PhysicalPagePoolRDG = GraphBuilder.RegisterExternalTexture(CacheManager->GetPhysicalPagePool());
		PhysicalPageMetaDataRDG = GraphBuilder.RegisterExternalBuffer(CacheManager->GetPhysicalPageMetaData());
		UniformParameters.PhysicalPagePool = PhysicalPagePoolRDG;

		UniformParameters.CachePrimitiveAsDynamic = GraphBuilder.CreateSRV(CacheManager->UploadCachePrimitiveAsDynamic(GraphBuilder));
	}
	else
	{
		if (CacheManager)
		{
			CacheManager->FreePhysicalPool(GraphBuilder);
		}
		UniformParameters.PhysicalPagePool = GSystemTextures.GetZeroUIntArrayAtomicCompatDummy(GraphBuilder);
	}

	if (bEnabled && bUseHzbOcclusion)
	{
		const int HZBPoolArraySize = HasSeparateDynamicHZB() ? 2 : 1;
		HZBPhysicalArray = CacheManager->SetHZBPhysicalPoolSize(GraphBuilder, GetHZBPhysicalPoolSize(), HZBPoolArraySize, PF_R32_FLOAT);
		HZBPhysicalArrayRDG = GraphBuilder.RegisterExternalTexture(HZBPhysicalArray);
	}
	else
	{
		if (CacheManager)
		{
			CacheManager->FreeHZBPhysicalPool(GraphBuilder);
		}
		HZBPhysicalArray = nullptr;
		HZBPhysicalArrayRDG = nullptr;
	}

	UpdateCachedUniformBuffers(GraphBuilder);
}

int32 FVirtualShadowMapArray::Allocate(bool bSinglePageShadowMap, int32 Count)
{
	check(IsEnabled());
	int32 VirtualShadowMapId = INDEX_NONE;
	if (bSinglePageShadowMap)
	{
		if (ensure((NumSinglePageShadowMaps + Count) <= VSM_MAX_SINGLE_PAGE_SHADOW_MAPS))
		{
			VirtualShadowMapId = NumSinglePageShadowMaps;
			NumSinglePageShadowMaps += Count;
		}
	}
	else
	{
		// Full shadow maps come after single page shadow maps
		VirtualShadowMapId = NumShadowMapSlots;
		NumShadowMapSlots += Count;
	}

	return VirtualShadowMapId;
}

FVirtualShadowMapArray::~FVirtualShadowMapArray()
{
}

EPixelFormat FVirtualShadowMapArray::GetPackedShadowMaskFormat() const
{
	// TODO: Check if we're after any point that determines the format later too (light setup)
	check(bInitialized);
	// NOTE: Currently 4bpp/light
	if (UniformParameters.PackedShadowMaskMaxLightCount <= 8)
	{
		return PF_R32_UINT;
	}
	else if (UniformParameters.PackedShadowMaskMaxLightCount <= 16)
	{
		return PF_R32G32_UINT;
	}
	else
	{
		check(UniformParameters.PackedShadowMaskMaxLightCount <= 32);
		return PF_R32G32B32A32_UINT;
	}
}

FIntPoint FVirtualShadowMapArray::GetPhysicalPoolSize() const
{
	check(bInitialized);
	return FIntPoint(UniformParameters.PhysicalPoolSize.X, UniformParameters.PhysicalPoolSize.Y);
}

FIntPoint FVirtualShadowMapArray::GetHZBPhysicalPoolSize() const
{
	check(bInitialized);
	FIntPoint PhysicalPoolSize = GetPhysicalPoolSize();
	FIntPoint HZBSize(FMath::Max(FPlatformMath::RoundUpToPowerOfTwo(PhysicalPoolSize.X) >> 1, 1u),
	                  FMath::Max(FPlatformMath::RoundUpToPowerOfTwo(PhysicalPoolSize.Y) >> 1, 1u));
	return HZBSize;
}

uint32 FVirtualShadowMapArray::GetTotalAllocatedPhysicalPages() const
{
	check(bInitialized);
	return ShouldCacheStaticSeparately() ? (2U * UniformParameters.MaxPhysicalPages) : UniformParameters.MaxPhysicalPages;
}

TRDGUniformBufferRef<FVirtualShadowMapUniformParameters> FVirtualShadowMapArray::GetUncachedUniformBuffer(FRDGBuilder& GraphBuilder) const
{
	// NOTE: Need to allocate new parameter space since the UB changes over the frame as dummy references are replaced
	FVirtualShadowMapUniformParameters* VersionedParameters = GraphBuilder.AllocParameters<FVirtualShadowMapUniformParameters>();
	*VersionedParameters = UniformParameters;
	return GraphBuilder.CreateUniformBuffer(VersionedParameters);
}

void FVirtualShadowMapArray::UpdateCachedUniformBuffers(FRDGBuilder& GraphBuilder)
{
	CachedUniformBuffers.Reset();

	// If we haven't yet initialized per-view parameters and are still using dummy data
	if (PerViewParameters.Num() == 0)
	{
		CachedUniformBuffers.Add(GetUncachedUniformBuffer(GraphBuilder));
}
	// If per-view parameters are initialized
	else
	{
		CachedUniformBuffers.SetNum(PerViewParameters.Num());

		for (int ViewIndex = 0; ViewIndex < PerViewParameters.Num(); ++ViewIndex)
		{
			FVirtualShadowMapUniformParameters* VersionedParameters = GraphBuilder.AllocParameters<FVirtualShadowMapUniformParameters>(&UniformParameters);
			VersionedParameters->PerViewData = PerViewParameters[ViewIndex];
			CachedUniformBuffers[ViewIndex] = GraphBuilder.CreateUniformBuffer(VersionedParameters);
		}
	}
}

void FVirtualShadowMapArray::SetShaderDefines(FShaderCompilerEnvironment& OutEnvironment)
{
	static_assert(FVirtualShadowMap::Log2Level0DimPagesXY * 2U + NANITE_MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS <= 32U, "Page indirection plus view index must fit into 32-bits for page-routing storage!");
	OutEnvironment.SetDefine(TEXT("ENABLE_NON_NANITE_VSM"), GEnableNonNaniteVSM);
	OutEnvironment.SetDefine(TEXT("MAX_PAGE_AREA_DIAGNOSTIC_SLOTS"), MaxPageAreaDiagnosticSlots);
	OutEnvironment.SetDefine(TEXT("INDEX_NONE"), INDEX_NONE);
}

FVirtualShadowMapSamplingParameters FVirtualShadowMapArray::GetSamplingParameters(FRDGBuilder& GraphBuilder, int32 ViewIndex) const
{
	// Sanity check: either VSMs are disabled and it's expected to be relying on dummy data, or we should have valid data
	// If this fires, it is likely because the caller is trying to sample VSMs before they have been rendered by the ShadowDepths pass
	// This should not crash, but it is not an intended production path as it will not return valid shadow data.
	// TODO: Disabled warning until SkyAtmosphereLUT is moved after ShadowDepths
	//ensureMsgf(!IsEnabled() || IsAllocated(),
	//	TEXT("Attempt to use Virtual Shadow Maps before they have been rendered by ShadowDepths."));

	FVirtualShadowMapSamplingParameters Parameters;
	Parameters.VirtualShadowMap = GetUniformBuffer(ViewIndex);
	return Parameters;
}

class FPruneLightGridCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FPruneLightGridCS);
	SHADER_USE_PARAMETER_STRUCT(FPruneLightGridCS, FVirtualShadowMapPageManagementShader)
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER(uint32, MinLocalLightIndex)
		SHADER_PARAMETER(uint32, MaxLocalLightIndex)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPrunedLightGridData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPrunedNumCulledLightsGrid)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPruneLightGridCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPageMarking.usf", "PruneLightGridCS", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FBaseGeneratePageFlagsParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageRequestFlags)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, DirectionalLightIds)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
	SHADER_PARAMETER(float, PageDilationBorderSizeDirectional)
	SHADER_PARAMETER(float, PageDilationBorderSizeLocal)
	SHADER_PARAMETER(uint32, bCullBackfacingPixels)
	SHADER_PARAMETER(uint32, NumDirectionalLightSmInds)
	SHADER_PARAMETER(uint32, MipModeLocal)
END_SHADER_PARAMETER_STRUCT()

class FGeneratePageFlagsFromPixelsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FGeneratePageFlagsFromPixelsCS);
	SHADER_USE_PARAMETER_STRUCT(FGeneratePageFlagsFromPixelsCS, FVirtualShadowMapPageManagementShader)

	class FInputType : SHADER_PERMUTATION_INT("PERMUTATION_INPUT_TYPE", 2); 
	class FWaterDepth : SHADER_PERMUTATION_BOOL("PERMUTATION_WATER_DEPTH"); 
	class FTranslucencyDepth : SHADER_PERMUTATION_BOOL("PERMUTATION_TRANSLUCENCY_DEPTH"); 
	using FPermutationDomain = TShaderPermutationDomain<FInputType, FWaterDepth, FTranslucencyDepth>;
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FInputType>() != 0 && (PermutationVector.Get<FWaterDepth>() || PermutationVector.Get<FTranslucencyDepth>()))
		{
			return false;
		}
		return FVirtualShadowMapPageManagementShader::ShouldCompilePermutation(Parameters);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBaseGeneratePageFlagsParameters, Base)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		// PERMUTATION_WATER_DEPTH
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SingleLayerWaterDepthTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, SingleLayerWaterTileMask)
		SHADER_PARAMETER(FIntPoint, SingleLayerWaterTileViewRes)
		// PERMUTATION_TRANSLUCENCY_DEPTH
		// FRONT LAYER 
		SHADER_PARAMETER(uint32, FrontLayerMode)
		SHADER_PARAMETER(FVector4f, FrontLayerHistoryUVMinMax)
		SHADER_PARAMETER(FVector4f, FrontLayerHistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4f, FrontLayerHistoryBufferSizeAndInvSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FrontLayerTranslucencyDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FrontLayerTranslucencyNormalTexture)

		RDG_BUFFER_ACCESS(IndirectBufferArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(uint32, InputType)
		SHADER_PARAMETER(FIntPoint, PixelStride)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGeneratePageFlagsFromPixelsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPageMarking.usf", "GeneratePageFlagsFromPixels", SF_Compute);

class FGeneratePageFlagsFromFroxelsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FGeneratePageFlagsFromFroxelsCS);
	SHADER_USE_PARAMETER_STRUCT(FGeneratePageFlagsFromFroxelsCS, FVirtualShadowMapPageManagementShader)

	class FDebugRenderDim : SHADER_PERMUTATION_BOOL("DEBUG_DRAW_GENERATE_FROM_FROXELS"); 
	using FPermutationDomain = TShaderPermutationDomain<FDebugRenderDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBaseGeneratePageFlagsParameters, Base)
		SHADER_PARAMETER(uint32, bShouldMarkLocaLights)
		SHADER_PARAMETER(int32, PassId)
		SHADER_PARAMETER(float, DebugRange)
		SHADER_PARAMETER_STRUCT_INCLUDE(Froxel::FParameters, FroxelParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE( ShaderPrint::FShaderParameters, ShaderPrintStruct )
		RDG_BUFFER_ACCESS(IndirectBufferArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGeneratePageFlagsFromFroxelsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPageMarking.usf", "GeneratePageFlagsFromFroxelsCS", SF_Compute);


class FMarkCoarsePagesCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FMarkCoarsePagesCS);
	SHADER_USE_PARAMETER_STRUCT(FMarkCoarsePagesCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageRequestFlags)
		SHADER_PARAMETER(uint32, bMarkCoarsePagesLocal)
		SHADER_PARAMETER(uint32, bIncludeNonNaniteGeometry)
		SHADER_PARAMETER(int32, ClipmapFirstLevel)
		SHADER_PARAMETER(uint32, ClipmapIndexMask)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMarkCoarsePagesCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPageMarking.usf", "MarkCoarsePages", SF_Compute);


class FGenerateHierarchicalPageFlagsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FGenerateHierarchicalPageFlagsCS);
	SHADER_USE_PARAMETER_STRUCT(FGenerateHierarchicalPageFlagsCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPhysicalPageMetaData >, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, OutUncachedPageRectBounds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, OutAllocatedPageRectBounds)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FGenerateHierarchicalPageFlagsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPageManagement.usf", "GenerateHierarchicalPageFlags", SF_Compute);


class FUpdatePhysicalPageAddresses : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FUpdatePhysicalPageAddresses);
	SHADER_USE_PARAMETER_STRUCT(FUpdatePhysicalPageAddresses, FVirtualShadowMapPageManagementShader )

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim, FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPhysicalPageMetaData>,		OutPhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNextVirtualShadowMapData>,	NextVirtualShadowMapData )
		SHADER_PARAMETER(uint32,														NextVirtualShadowMapDataCount )		
		// Required if using FHasCacheDataDim
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,							PrevPageRequestFlags)
		// Required if using FGenerateStatsDim
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutStatsBuffer )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FUpdatePhysicalPageAddresses, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "UpdatePhysicalPageAddresses", SF_Compute );


class FUpdatePhysicalPages : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FUpdatePhysicalPages);
	SHADER_USE_PARAMETER_STRUCT(FUpdatePhysicalPages, FVirtualShadowMapPageManagementShader )

	class FHasCacheDataDim : SHADER_PERMUTATION_BOOL("HAS_CACHE_DATA");
	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS"); 
	using FPermutationDomain = TShaderPermutationDomain<FHasCacheDataDim, FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPhysicalPageMetaData >,	OutPhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< int >,						OutPhysicalPageLists )
		// Required if using FHasCacheDataDim
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,						PageRequestFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageTable )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< int >,						PrevPhysicalPageLists )
		SHADER_PARAMETER( uint32,														MaxPageAgeSinceLastRequest )
		// TODO: encode into options bitfield?
		SHADER_PARAMETER( int32,														bDynamicPageInvalidation )
		SHADER_PARAMETER( int32,														bAllocateViaLRU )
		// Required if using FGenerateStatsDim
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutStatsBuffer )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FUpdatePhysicalPages, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "UpdatePhysicalPages", SF_Compute );

/**
 * Utility to help schedule kernels that do processing for each page to facilitate not processing mip levels that can't be reached.
 */
class FVirtualShadowMapPerPageShader : public FVirtualShadowMapPageManagementShader
{
public:
	FVirtualShadowMapPerPageShader()
	{
	}

	FVirtualShadowMapPerPageShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FVirtualShadowMapPageManagementShader(Initializer)
	{
	}

	static constexpr int32 ThreadGroupSizeX = 64;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >, VirtualShadowMapIds)
		SHADER_PARAMETER( uint32, VirtualShadowMapIdsOffset ) 
		SHADER_PARAMETER( uint32, NumVirtualShadowMapIds ) 
		SHADER_PARAMETER( uint32, PerPageDispatchDimX ) 
		SHADER_PARAMETER( uint32, bUseThreadPerId ) 
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("PER_PAGE_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("PER_PAGE_DISPATCH_SETUP"), 1);
		FVirtualShadowMapPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	class FDispatcher
	{
	public:

		struct FBinConfig
		{
			int32 DispatchDimX;
		};

		struct FBin
		{
			int32 VirtualShadowMapIdsOffset = 0;
			int32 NumVirtualShadowMapIds = 0;
		};

		// 1, Small, medium, large
		static constexpr int32 BinCount = 4;
		static constexpr int32 BinDispatchDimX[BinCount] = 
		{
			4096,
			512,
			64,
			1
		};

		static int32 CalcBin(int32 VirtualShadowMapId, int32 MinMipLevel)
		{
			if (IsSinglePageVirtualShadowMap(VirtualShadowMapId))
			{
				// last bin, single thread per shadow map
				return BinCount - 1;
			}

			if (MinMipLevel < 6)
			{
				return MinMipLevel / 2; 
			}

			// last bin, single thread per shadow map
			return BinCount - 1;
		}

		TStaticArray<FBin, BinCount> Bins;
		FRDGBufferSRV* VirtualShadowMapIdsSRV = nullptr;

		struct FBuilder
		{
			struct FIdBinIndex
			{
				uint32 VirtualShadowMapId : 28;
				uint32 BinIndex : 4;
			};

			void Add(int32 VirtualShadowMapId, uint32 MinMipLevel)
			{
				if (VirtualShadowMapId != INDEX_NONE)
				{
					FIdBinIndex IdBinIndex;
					IdBinIndex.BinIndex = CalcBin(VirtualShadowMapId, MinMipLevel);
					IdBinIndex.VirtualShadowMapId = VirtualShadowMapId;
					Tmp.Add(IdBinIndex);
					Bins[IdBinIndex.BinIndex].NumVirtualShadowMapIds += 1;
				}

			}

			void Reserve(int32 Num)
			{
				Tmp.Reserve(Num);
			}

			TStaticArray<FBin, BinCount> Bins;
			TArray<FIdBinIndex,SceneRenderingAllocator> Tmp;
		};

		void Init(FRDGBuilder& GraphBuilder, const FBuilder& Builder)
		{
			Bins = Builder.Bins;
			// counting sort the temp data into buffer of IDs
			int32 Offset = 0;
			for (int32 BinIndex = 0; BinIndex < BinCount; ++BinIndex)
			{
				Bins[BinIndex].VirtualShadowMapIdsOffset = Offset;
				Offset += Bins[BinIndex].NumVirtualShadowMapIds;
				Bins[BinIndex].NumVirtualShadowMapIds = 0;
			}
			TArray<uint32,SceneRenderingAllocator> VirtualShadowMapIds;
			VirtualShadowMapIds.SetNumUninitialized(Builder.Tmp.Num());
			for (FBuilder::FIdBinIndex IdBinIndex : Builder.Tmp)
			{
				FBin &Bin = Bins[IdBinIndex.BinIndex];
				VirtualShadowMapIds[Bin.VirtualShadowMapIdsOffset + Bin.NumVirtualShadowMapIds++] = IdBinIndex.VirtualShadowMapId;
			}

			VirtualShadowMapIdsSRV = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.PerPageBinIds"), MoveTemp(VirtualShadowMapIds)));
		}

		template <typename TShaderClass>
		void AddPass(FRDGBuilder& GraphBuilder, FRDGEventName&& PassName, const TShaderRef<TShaderClass>& ComputeShader, typename TShaderClass::FParameters *Parameters)
		{
			const FShaderParametersMetadata* ParametersMetadata = TShaderClass::FParameters::FTypeInfo::GetStructMetadata();
			Parameters->VirtualShadowMapPerPageShader.VirtualShadowMapIds = VirtualShadowMapIdsSRV;
			ClearUnusedGraphResources(ComputeShader, ParametersMetadata, Parameters);

			GraphBuilder.AddPass(Forward<FRDGEventName>(PassName), ParametersMetadata, Parameters, ERDGPassFlags::Compute,
			[ParametersMetadata, Parameters, ComputeShader, Bins = Bins](FRHIComputeCommandList& RHICmdList)
			{
				for (int32 BinIndex = 0; BinIndex < BinCount; ++BinIndex)
				{
					const FBin &Bin = Bins[BinIndex];
					if (Bins[BinIndex].NumVirtualShadowMapIds > 0)
					{
						bool bUseThreadPerId = BinDispatchDimX[BinIndex] == 1;
						int32 ThreadPerIdDispatchDimX = ThreadGroupSizeX * FMath::DivideAndRoundUp(Bin.NumVirtualShadowMapIds, ThreadGroupSizeX);
						int32 PerPageDispatchDimX = bUseThreadPerId ? ThreadPerIdDispatchDimX : BinDispatchDimX[BinIndex];

						// Set args for each pass
						Parameters->VirtualShadowMapPerPageShader.PerPageDispatchDimX = PerPageDispatchDimX; 
						Parameters->VirtualShadowMapPerPageShader.VirtualShadowMapIdsOffset = Bin.VirtualShadowMapIdsOffset;
						Parameters->VirtualShadowMapPerPageShader.NumVirtualShadowMapIds = Bin.NumVirtualShadowMapIds;
						Parameters->VirtualShadowMapPerPageShader.bUseThreadPerId = bUseThreadPerId ? 1 : 0;

						FIntVector GroupCount = FIntVector(0,0,1);
						int32 NumGroupsY = 1;
						if (!bUseThreadPerId)
						{
							NumGroupsY = Bin.NumVirtualShadowMapIds;
						}

						FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, ParametersMetadata, *Parameters, FIntVector(FMath::DivideAndRoundUp(PerPageDispatchDimX, ThreadGroupSizeX), NumGroupsY, 1));
					}
				}

			});

		}
	};
};

class FClearPageTableCS : public FVirtualShadowMapPerPageShader
{
	DECLARE_GLOBAL_SHADER(FClearPageTableCS);
	SHADER_USE_PARAMETER_STRUCT(FClearPageTableCS, FVirtualShadowMapPerPageShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,		VirtualShadowMap )
		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualShadowMapPerPageShader::FParameters,	VirtualShadowMapPerPageShader)
		SHADER_PARAMETER( uint32,														ClearValue )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutDestBuffer )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FClearPageTableCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "ClearPageTableCS", SF_Compute);


class FAllocateNewPageMappingsCS : public FVirtualShadowMapPerPageShader
{
	DECLARE_GLOBAL_SHADER(FAllocateNewPageMappingsCS);
	SHADER_USE_PARAMETER_STRUCT(FAllocateNewPageMappingsCS, FVirtualShadowMapPerPageShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS"); 
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,		VirtualShadowMap )
		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualShadowMapPerPageShader::FParameters,	VirtualShadowMapPerPageShader)
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< uint >,						PageRequestFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageFlags )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageTable )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< int >,						OutPhysicalPageLists )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FPhysicalPageMetaData >,	OutPhysicalPageMetaData )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutStatsBuffer )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAllocateNewPageMappingsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "AllocateNewPageMappingsCS", SF_Compute);

class FPackAvailablePagesCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FPackAvailablePagesCS);
	SHADER_USE_PARAMETER_STRUCT(FPackAvailablePagesCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters,			VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>,						OutPhysicalPageLists)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,						OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPackAvailablePagesCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "PackAvailablePages", SF_Compute );

class FAppendPhysicalPageListsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FAppendPhysicalPageListsCS);
	SHADER_USE_PARAMETER_STRUCT(FAppendPhysicalPageListsCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters,			VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,						OutPhysicalPageLists)
		SHADER_PARAMETER(uint32, bAppendEmptyToAvailable)
		SHADER_PARAMETER(uint32, bUpdateCounts)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAppendPhysicalPageListsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "AppendPhysicalPageLists", SF_Compute );

class FPropagateMappedMipsCS : public FVirtualShadowMapPerPageShader
{
	DECLARE_GLOBAL_SHADER(FPropagateMappedMipsCS);
	SHADER_USE_PARAMETER_STRUCT(FPropagateMappedMipsCS, FVirtualShadowMapPerPageShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER( FVirtualShadowMapUniformParameters,		VirtualShadowMap )
		SHADER_PARAMETER_STRUCT_INCLUDE( FVirtualShadowMapPerPageShader::FParameters,	VirtualShadowMapPerPageShader)
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< uint >,					OutPageTable )
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPropagateMappedMipsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPageManagement.usf", "PropagateMappedMips", SF_Compute);

class FSelectPagesToInitializeCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSelectPagesToInitializeCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectPagesToInitializeCS, FVirtualShadowMapPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutInitializePagesIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPhysicalPagesToInitialize)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectPagesToInitializeCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "SelectPagesToInitializeCS", SF_Compute);

class FInitializePhysicalPagesIndirectCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitializePhysicalPagesIndirectCS);
	SHADER_USE_PARAMETER_STRUCT(FInitializePhysicalPagesIndirectCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesToInitialize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPhysicalPagePool)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitializePhysicalPagesIndirectCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "InitializePhysicalPagesIndirectCS", SF_Compute);

class FSelectPagesToMergeCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSelectPagesToMergeCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectPagesToMergeCS, FVirtualShadowMapPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutMergePagesIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPhysicalPagesToMerge)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectPagesToMergeCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "SelectPagesToMergeCS", SF_Compute);

class FMergeStaticPhysicalPagesIndirectCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FMergeStaticPhysicalPagesIndirectCS);
	SHADER_USE_PARAMETER_STRUCT(FMergeStaticPhysicalPagesIndirectCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesToMerge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutPhysicalPagePool)
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FMergeStaticPhysicalPagesIndirectCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "MergeStaticPhysicalPagesIndirectCS", SF_Compute);



void FVirtualShadowMapArray::MergeStaticPhysicalPages(FRDGBuilder& GraphBuilder)
{
	check(IsEnabled());
	if (GetNumShadowMaps() == 0 || !ShouldCacheStaticSeparately())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (CVarDebugSkipMergePhysical.GetValueOnRenderThread() != 0)
	{
		return;
	}
#endif

	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::MergeStaticPhysicalPages");

	// Note: We use GetTotalAllocatedPhysicalPages() to size the buffer as the selection shader emits both static/dynamic pages separately when enabled.
	FRDGBufferRef PhysicalPagesToMergeRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetTotalAllocatedPhysicalPages() + 1), TEXT("Shadow.Virtual.PhysicalPagesToMerge"));

	// 1. Initialize the indirect args buffer
	FRDGBufferRef MergePagesIndirectArgsRDG = CreateAndClearIndirectDispatchArgs1D(GraphBuilder, Scene.GetFeatureLevel(), TEXT("Shadow.Virtual.MergePagesIndirectArgs"));

	// 2. Filter the relevant physical pages and set up the indirect args
	{
		FSelectPagesToMergeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectPagesToMergeCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
		PassParameters->OutMergePagesIndirectArgsBuffer = GraphBuilder.CreateUAV(MergePagesIndirectArgsRDG);
		PassParameters->OutPhysicalPagesToMerge = GraphBuilder.CreateUAV(PhysicalPagesToMergeRDG);

		FSelectPagesToMergeCS::FPermutationDomain PermutationVector;
		SetStatsArgsAndPermutation<FSelectPagesToMergeCS>(ShouldGenerateStats(), StatsBufferUAV, PassParameters, PermutationVector);

		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FSelectPagesToMergeCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SelectPagesToMerge"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FSelectPagesToMergeCS::DefaultCSGroupX), 1, 1)
		);

	}
	// 3. Indirect dispatch to clear the selected pages
	{
		FMergeStaticPhysicalPagesIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMergeStaticPhysicalPagesIndirectCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutPhysicalPagePool = GraphBuilder.CreateUAV(PhysicalPagePoolRDG);
		PassParameters->IndirectArgs = MergePagesIndirectArgsRDG;
		PassParameters->PhysicalPagesToMerge = GraphBuilder.CreateSRV(PhysicalPagesToMergeRDG);
		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FMergeStaticPhysicalPagesIndirectCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("MergeStaticPhysicalPagesIndirect"),
			ComputeShader,
			PassParameters,
			PassParameters->IndirectArgs,
			0
		);
	}
}


class FInitPageRectBoundsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FInitPageRectBoundsCS);
	SHADER_USE_PARAMETER_STRUCT(FInitPageRectBoundsCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, OutUncachedPageRectBounds)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FIntVector4>, OutAllocatedPageRectBounds)
		SHADER_PARAMETER(uint32, NumPageRectsToClear)		
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int>, OutPhysicalPageLists)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPhysicalPageRequest>, OutPhysicalPageAllocationRequests)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitPageRectBoundsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "InitPageRectBounds", SF_Compute);


class FVirtualSmFeedbackStatusCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmFeedbackStatusCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmFeedbackStatusCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< int >, PhysicalPageLists)
		SHADER_PARAMETER_STRUCT_INCLUDE(GPUMessage::FParameters, GPUMessageParams)
		SHADER_PARAMETER(uint32, StatusMessageId)
	END_SHADER_PARAMETER_STRUCT()


	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualShadowMapPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmFeedbackStatusCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "FeedbackStatusCS", SF_Compute);

void FVirtualShadowMapVisualizeLightSearch::CheckLight(const FLightSceneProxy* CheckProxy, int CheckVirtualShadowMapId)
{
#if !UE_BUILD_SHIPPING
	FString CheckLightName = CheckProxy->GetOwnerNameOrLabel();
	if (GDumpVSMLightNames)
	{
		UE_LOG(LogRenderer, Display, TEXT("%s"), *CheckLightName);
	}

	const UPTRINT CheckProxyId = (UPTRINT)(CheckProxy->GetLightComponent());

	const int SelectAdjacentVisualizeLight = GetVirtualShadowMapVisualizationData().SelectAdjacentVisualizeLight;
	const bool SelectNextLight = SelectAdjacentVisualizeLight > 0;
	const bool SelectPrevLight = SelectAdjacentVisualizeLight < 0;
	GVirtualShadowMapVisualizeByLightId = GVirtualShadowMapVisualizeByLightId || SelectAdjacentVisualizeLight != 0;

	// When the user clicks a light, visualize the selected light.
	if (CheckProxy->IsSelected() && CheckProxyId != GVirtualShadowMapLastSelectedVisualizeLightId)
	{
		GVirtualShadowMapVisualizeByLightId = false;
		GVirtualShadowMapLastSelectedVisualizeLightId = CheckProxyId;
	}

	if (GVirtualShadowMapVisualizeByLightId)
	{
		UPTRINT FoundAdjacentLightId = FoundProxy ? (UPTRINT)(FoundProxy->GetLightComponent()) : 0;
		bool bIsFoundAdjacentLightInvalid = FoundAdjacentLightId == 0;

		if (SelectPrevLight
			// Light comes before current selection
			&& CheckProxyId < GVirtualShadowMapVisualizeLightId
			// Light comes after best match so far
			&& FoundAdjacentLightId < CheckProxyId)
		{
			FoundProxy = CheckProxy;
			FoundVirtualShadowMapId = CheckVirtualShadowMapId;
		}
		else if (SelectNextLight
			// Light comes after current selection
			&& GVirtualShadowMapVisualizeLightId < CheckProxyId
			// Light comes before best match so far
			&& (CheckProxyId < FoundAdjacentLightId || bIsFoundAdjacentLightInvalid))
		{
			FoundProxy = CheckProxy;
			FoundVirtualShadowMapId = CheckVirtualShadowMapId;
		}
		else if (SelectAdjacentVisualizeLight == 0 
			&& CheckProxyId == GVirtualShadowMapVisualizeLightId)
		{
			FoundProxy = CheckProxy;
			FoundVirtualShadowMapId = CheckVirtualShadowMapId;
		}
	}
	else
	{
		// Fill out new sort key and compare to our best found so far
		SortKey CheckKey;
		CheckKey.Packed = 0;
		CheckKey.Fields.bExactNameMatch = (CheckLightName == GVirtualShadowMapVisualizeLightName);
		CheckKey.Fields.bPartialNameMatch = CheckKey.Fields.bExactNameMatch || (!GVirtualShadowMapVisualizeLightName.IsEmpty() && CheckLightName.Contains(GVirtualShadowMapVisualizeLightName));
		CheckKey.Fields.bSelected = CheckProxy->IsSelected();

		if (CheckKey.Packed > FoundKey.Packed)		//-V547
		{
			FoundKey = CheckKey;
			FoundProxy = CheckProxy;
			FoundVirtualShadowMapId = CheckVirtualShadowMapId;
		}
	}
#endif
}

void FVirtualShadowMapVisualizeLightSearch::ChooseLight()
{
#if !UE_BUILD_SHIPPING
	const int SelectAdjacentVisualizeLight = GetVirtualShadowMapVisualizationData().SelectAdjacentVisualizeLight;
	GetVirtualShadowMapVisualizationData().SelectAdjacentVisualizeLight = 0;

	if (FoundProxy)
	{
		GVirtualShadowMapVisualizeLightId = (UPTRINT)(FoundProxy->GetLightComponent());
	}
	else if (SelectAdjacentVisualizeLight) // selected past first or last light, select none
	{
		GVirtualShadowMapVisualizeLightId = 0;
	}
#endif
}

const FString FVirtualShadowMapVisualizeLightSearch::GetLightName() const
{
	return FoundProxy->GetOwnerNameOrLabel();
}

static FRDGTextureRef CreateDebugVisualizationTexture(FRDGBuilder& GraphBuilder, FIntPoint Extent)
{
	const FLinearColor ClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
		Extent,
		PF_FloatRGBA,
		FClearValueBinding(ClearColor),
		TexCreate_ShaderResource | TexCreate_UAV);

	FRDGTextureRef Texture = GraphBuilder.CreateTexture(Desc, TEXT("Shadow.Virtual.DebugProjection"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Texture), ClearColor);
	return Texture;
}

void FVirtualShadowMapArray::UpdateVisualizeLight(
	const TConstArrayView<FViewInfo> &Views,
	const TConstArrayView<FVisibleLightInfo>& VisibleLightInfos)
{
#if !UE_BUILD_SHIPPING
	for (int32 LightId = 0; LightId < VisibleLightInfos.Num(); ++LightId)
	{
		const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightId];
		for (const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap : VisibleLightInfo.VirtualShadowMapClipmaps)
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (Clipmap->GetDependentView() == Views[ViewIndex].GetPrimaryView())
				{
					VisualizeLight[ViewIndex].CheckLight(Clipmap->GetLightSceneInfo().Proxy, Clipmap->GetVirtualShadowMapId());
				}
			}
		}

		for (FProjectedShadowInfo* ProjectedShadowInfo : VisibleLightInfo.AllProjectedShadows)
		{
			// NOTE: Specifically checking the VirtualShadowMapId vs HasVirtualShadowMap() here as clipmaps are handled above
			if (ProjectedShadowInfo->VirtualShadowMapId != INDEX_NONE)
			{
				check(ProjectedShadowInfo->CascadeSettings.ShadowSplitIndex == INDEX_NONE);		// We use clipmaps for virtual shadow maps, not cascades

				// NOTE: Virtual shadow maps are never atlased, but verify our assumptions
				{
					const FVector4f ClipToShadowUV = ProjectedShadowInfo->GetClipToShadowBufferUvScaleBias();
					check(ProjectedShadowInfo->BorderSize == 0);
					check(ProjectedShadowInfo->X == 0);
					check(ProjectedShadowInfo->Y == 0);
					const FIntRect ShadowViewRect = ProjectedShadowInfo->GetInnerViewRect();
					check(ShadowViewRect.Min.X == 0);
					check(ShadowViewRect.Min.Y == 0);
					check(ShadowViewRect.Max.X == FVirtualShadowMap::VirtualMaxResolutionXY);
					check(ShadowViewRect.Max.Y == FVirtualShadowMap::VirtualMaxResolutionXY);
				}

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					VisualizeLight[ViewIndex].CheckLight(ProjectedShadowInfo->GetLightSceneInfo().Proxy, ProjectedShadowInfo->VirtualShadowMapId);
				}
			}
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		VisualizeLight[ViewIndex].ChooseLight();
	}
#endif
}

void FVirtualShadowMapArray::AppendPhysicalPageList(FRDGBuilder& GraphBuilder, bool bEmptyToAvailable)
{
	auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FAppendPhysicalPageListsCS>();
	
	FAppendPhysicalPageListsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FAppendPhysicalPageListsCS::FParameters>();
	PassParameters->VirtualShadowMap		= GetUncachedUniformBuffer(GraphBuilder);
	PassParameters->OutPhysicalPageLists	= GraphBuilder.CreateUAV(PhysicalPageListsRDG);
	PassParameters->bAppendEmptyToAvailable = bEmptyToAvailable ? 1 : 0;
	PassParameters->bUpdateCounts			= 0;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("AppendPhysicalPageList"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FAppendPhysicalPageListsCS::DefaultCSGroupX), 1, 1)
	);

	FAppendPhysicalPageListsCS::FParameters* CountsParameters = GraphBuilder.AllocParameters<FAppendPhysicalPageListsCS::FParameters>();
	*CountsParameters = *PassParameters;
	CountsParameters->bUpdateCounts = 1;
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("AppendPhysicalPageList(Counts)"),
		ComputeShader,
		CountsParameters,
		FIntVector(1, 1, 1)
	);
}

void FVirtualShadowMapArray::UpdatePhysicalPageAddresses(FRDGBuilder& GraphBuilder)
{
	if (!IsEnabled())
	{
		return;
	}

	// First, let the cache manager update any that may not be referenced this frame but may still have cached pages.
	// TODO: Store the number of active lights we have first this frame for GPU looping purposes.
	// By construction unreferenced lights are at the end.
	CacheManager->UpdateUnreferencedCacheEntries(*this);

	// NOTE: This past MUST run on all GPUs, as we still need to propogate changes to the VSM IDs even if
	// a given GPU may not do any rendering during this phase.
	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	FUpdatePhysicalPageAddresses::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdatePhysicalPageAddresses::FParameters>();
	PassParameters->VirtualShadowMap		= GetUniformBuffer(0);
	PassParameters->OutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);

	// Upload our prev -> next shadow data mapping (FNextVirtualShadowMapData) to the GPU
	FRDGBufferRef NextVirtualShadowMapData = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.NextVirtualShadowMapData"), NextData);
	PassParameters->NextVirtualShadowMapData = GraphBuilder.CreateSRV(NextVirtualShadowMapData);
	PassParameters->NextVirtualShadowMapDataCount = NextData.Num();

	FUpdatePhysicalPageAddresses::FPermutationDomain PermutationVector;

	TRefCountPtr<FRDGPooledBuffer> PrevPageRequestFlags = CacheManager->GetPrevBuffers().PageRequestFlags;
	if (PrevPageRequestFlags != nullptr)
	{
		PassParameters->PrevPageRequestFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(PrevPageRequestFlags));
	}
	PermutationVector.Set<FUpdatePhysicalPageAddresses::FHasCacheDataDim>(PrevPageRequestFlags != nullptr);

	SetStatsArgsAndPermutation<FUpdatePhysicalPageAddresses>(ShouldGenerateStats(), StatsBufferUAV, PassParameters, PermutationVector);

	auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FUpdatePhysicalPageAddresses>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("FVirtualShadowMapArray::UpdatePhysicalPageAddresses"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FUpdatePhysicalPageAddresses::DefaultCSGroupX), 1, 1)
	);
}

bool FVirtualShadowMapArray::ShouldGenerateStats() const
{
#if !UE_BUILD_SHIPPING
	const bool bRunPageAreaDiagnostics = CVarNumPageAreaDiagSlots.GetValueOnRenderThread() != 0;
#else
	constexpr bool bRunPageAreaDiagnostics = false;
#endif
	return CVarShowStats.GetValueOnRenderThread() || CacheManager->IsAccumulatingStats() || bRunPageAreaDiagnostics || IsCsvLogEnabled();
}

bool FVirtualShadowMapArray::IsCsvLogEnabled() const
{
#if CSV_PROFILER_STATS
	return FCsvProfiler::Get()->IsCapturing_Renderthread() && FCsvProfiler::Get()->IsCategoryEnabled(CSV_CATEGORY_INDEX(VSM));
#else
	return false;
#endif
}

void FVirtualShadowMapArray::BuildPageAllocations(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const TConstArrayView<FViewInfo>& Views,
	const FSortedLightSetSceneInfo& SortedLightsInfo,
	const TConstArrayView<FVisibleLightInfo>& VisibleLightInfos,
	const FSingleLayerWaterPrePassResult* SingleLayerWaterPrePassResult,
	const FFrontLayerTranslucencyData& FrontLayerTranslucencyData,
	const Froxel::FRenderer& FroxelRenderer,
	bool bAnyLocalLightsWithVSMs)
{
	check(IsEnabled());

	if (GetNumShadowMaps() == 0 || Views.Num() == 0)
	{
		// Nothing to do
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "FVirtualShadowMapArray::BuildPageAllocation");
	SCOPED_NAMED_EVENT(FVirtualShadowMapArray_BuildPageAllocation, FColor::Emerald);

	VisualizeLight.Reset(Views.Num());
	VisualizeLight.AddDefaulted(Views.Num());

	PerViewParameters.Reset(Views.Num());
	PerViewParameters.AddDefaulted(Views.Num());

#if !UE_BUILD_SHIPPING
	if (GDumpVSMLightNames)
	{
		UE_LOG(LogRenderer, Display, TEXT("Lights with Virtual Shadow Maps:"));
	}

	// Setup debug visualization output if enabled
	if (bEnableVisualization)
	{
		FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
	
		for (const FViewInfo& View : Views)
		{
			VisualizationData.Update(View.CurrentVirtualShadowMapVisualizationMode);
			if (VisualizationData.IsActive())
			{
				// for stereo views that aren't multi-view, don't account for the left
				FIntPoint Extent = View.ViewRect.Max - View.ViewRect.Min;
				DebugVisualizationOutput.Add(CreateDebugVisualizationTexture(GraphBuilder, Extent));
			}
		}
	}

	UpdateVisualizeLight(Views, VisibleLightInfos);
#endif //!UE_BUILD_SHIPPING

	FVirtualShadowMapPerPageShader::FDispatcher PerPageShaderDispatcher;
	{
		FVirtualShadowMapPerPageShader::FDispatcher::FBuilder Builder;

		// Create large enough to hold all the unused elements too (wastes GPU memory but allows direct indexing via the ID)
		uint32 DataSize = sizeof(FVirtualShadowMapProjectionShaderData) * uint32(GetNumShadowMapSlots());
		FRDGBufferDesc Desc;
		Desc.Usage = EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::ByteAddressBuffer | EBufferUsageFlags::StructuredBuffer;
		Desc.BytesPerElement = 4;
		Desc.NumElements = DataSize / 4U;
		ProjectionDataRDG = GraphBuilder.CreateBuffer(Desc, TEXT("Shadow.Virtual.ProjectionData"));
		
		FRDGScatterUploadBuffer Uploader;
		Uploader.Init(GraphBuilder, GetNumShadowMaps(), sizeof(FVirtualShadowMapProjectionShaderData), false, TEXT("Shadow.Virtual.ProjectionData.UploadBuffer"));

		for (auto It = CacheManager->CreateConstEntryIterator(); It; ++It)
		{
			const TSharedPtr<FVirtualShadowMapPerLightCacheEntry>& CacheEntry = It.Value();
			for (const auto& Entry : CacheEntry->ShadowMapEntries)
			{
				Uploader.Add(Entry.CurrentVirtualShadowMapId, &Entry.ProjectionData);
				Builder.Add(Entry.CurrentVirtualShadowMapId, Entry.ProjectionData.MinMipLevel);
			}
		}
		Uploader.ResourceUploadTo(GraphBuilder, ProjectionDataRDG);

		PerPageShaderDispatcher.Init(GraphBuilder, Builder);
	}

	// Stats
	SET_DWORD_STAT(STAT_VSMSinglePageCount, GetNumSinglePageShadowMaps());
	SET_DWORD_STAT(STAT_VSMFullCount, GetNumFullShadowMaps());
	// And _other_ stats...
	CSV_CUSTOM_STAT(VSM, SinglePageCount, GetNumSinglePageShadowMaps(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(VSM, FullCount, GetNumFullShadowMaps(), ECsvCustomStatOp::Set);

	UniformParameters.NumFullShadowMaps = GetNumFullShadowMaps();
	UniformParameters.NumSinglePageShadowMaps = GetNumSinglePageShadowMaps();
	UniformParameters.NumShadowMapSlots = GetNumShadowMapSlots();
	UniformParameters.ProjectionData = GraphBuilder.CreateSRV(ProjectionDataRDG);

	UniformParameters.bExcludeNonNaniteFromCoarsePages = !CVarCoarsePagesIncludeNonNanite.GetValueOnRenderThread();
	UniformParameters.CoarsePagePixelThresholdDynamic = CVarCoarsePagePixelThresholdDynamic.GetValueOnRenderThread();
	UniformParameters.CoarsePagePixelThresholdStatic = CVarCoarsePagePixelThresholdStatic.GetValueOnRenderThread();
	UniformParameters.CoarsePagePixelThresholdDynamicNanite = CVarCoarsePagePixelThresholdDynamicNanite.GetValueOnRenderThread();

	bool bCsvLogEnabled = IsCsvLogEnabled();

	// Stats buffer
	{
		StatsBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), VSM_STAT_NUM + MaxPageAreaDiagnosticSlots * 2), TEXT("Shadow.Virtual.StatsBuffer"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StatsBufferRDG), 0);

		// For the rest of the frame we don't want the stats buffer adding additional barriers that are not otherwise present.
		// Even though this is not a high performance path with stats enabled, we don't want to change behavior.
		StatsBufferUAV = GraphBuilder.CreateUAV(StatsBufferRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);
	}

	// We potentially over-allocate these to avoid too many different allocation sizes each frame
	const int32 NumPageFlagsToAllocate = FMath::RoundUpToPowerOfTwo(FMath::Max(128 * 1024, GetNumFullShadowMaps() * int32(FVirtualShadowMap::PageTableSize) + int32(VSM_MAX_SINGLE_PAGE_SHADOW_MAPS)));

	auto AddClearPageTableUAVPass = [&](FRDGBuilder& GraphBuilder, FRDGBufferUAV* DestUAV, uint32 ClearValue)
	{
		FClearPageTableCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FClearPageTableCS::FParameters >();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->ClearValue = ClearValue;
		PassParameters->OutDestBuffer = DestUAV;

		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FClearPageTableCS>();

		PerPageShaderDispatcher.AddPass(GraphBuilder, RDG_EVENT_NAME("ClearPageTable(%s Size=%ubytes)", DestUAV->GetParent()->Name, DestUAV->GetParent()->Desc.GetSize()), ComputeShader, PassParameters);
	};

	FRDGBufferRef InitializePagesIndirectArgsRDG = CreateAndClearIndirectDispatchArgs1D(GraphBuilder, Scene.GetFeatureLevel(), TEXT("Shadow.Virtual.InitializePagesIndirectArgs"));

	// Create and clear the requested page flags
	PageRequestFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlagsToAllocate), TEXT("Shadow.Virtual.PageRequestFlags"));
	AddClearPageTableUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageRequestFlagsRDG), 0);

	const uint32 DirtyFlagsPerPageCount = 4;
	DirtyPageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), GetMaxPhysicalPages() * DirtyFlagsPerPageCount), TEXT("Shadow.Virtual.DirtyPageFlags"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DirtyPageFlagsRDG), 0);

	// One additional element as the last element is used as an atomic counter
	const uint32 ItemsPerPhysicalPageList = GetMaxPhysicalPages() + 1;
	const uint32 PhysicalPageListsCount = 4;		// See VirtualShadowMapPhysicalPageManagement.usf
	PhysicalPageListsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), PhysicalPageListsCount * ItemsPerPhysicalPageList), TEXT("Shadow.Virtual.PhysicalPageLists"));

	const uint32 NumPageRects = GetNumShadowMapSlots() * FVirtualShadowMap::MaxMipLevels;
	const uint32 NumPageRectsToAllocate = FMath::RoundUpToPowerOfTwo(NumPageRects);
	UncachedPageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRectsToAllocate), TEXT("Shadow.Virtual.PageRectBounds"));
	AllocatedPageRectBoundsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector4), NumPageRectsToAllocate), TEXT("Shadow.Virtual.AllocatedPageRectBounds"));
	const uint32 NumPageRectsToClear = (GetNumFullShadowMaps() + GetNumSinglePageShadowMaps()) * FVirtualShadowMap::MaxMipLevels;
	{
		FInitPageRectBoundsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FInitPageRectBoundsCS::FParameters >();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutUncachedPageRectBounds = GraphBuilder.CreateUAV(UncachedPageRectBoundsRDG);
		PassParameters->OutAllocatedPageRectBounds = GraphBuilder.CreateUAV(AllocatedPageRectBoundsRDG);
		PassParameters->NumPageRectsToClear = NumPageRectsToClear;
		PassParameters->OutPhysicalPageLists = GraphBuilder.CreateUAV(PhysicalPageListsRDG);

		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FInitPageRectBoundsCS>();
		ClearUnusedGraphResources(ComputeShader, PassParameters);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitPageRectBounds"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(NumPageRectsToClear, FInitPageRectBoundsCS::DefaultCSGroupX), 1, 1)
		);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		const FViewInfo &View = Views[ViewIndex];
		FVirtualShadowMapPerViewParameters& PerViewData = PerViewParameters[ViewIndex];

		// Gather directional light virtual shadow maps
		TArray<int32, SceneRenderingAllocator> DirectionalLightIds;
		for (const FVisibleLightInfo& VisibleLightInfo : VisibleLightInfos)
		{
			for (const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap : VisibleLightInfo.VirtualShadowMapClipmaps)
			{
				// mark pages from both views in stereo for the left (primary) view clipmap - assumes GetPrimaryView returns self for non-stereo views.
				if (Clipmap->GetDependentView() == Views[ViewIndex].GetPrimaryView())
				{
					// NOTE: Shader assumes all levels from a given clipmap are contiguous
					int32 ClipmapID = Clipmap->GetVirtualShadowMapId();
					DirectionalLightIds.Add(ClipmapID);
				}
			}
		}	
		
		// This view contained no local lights (that were stored in the light grid), and no directional lights, so nothing to do.
		if (View.ForwardLightingResources.LocalLightVisibleLightInfosIndex.Num() + DirectionalLightIds.Num() == 0)
		{
			PerViewData = MakeEmptyVirtualShadowMapPerViewParameters(GraphBuilder);
			continue;
		}

		FRDGBufferRef DirectionalLightIdsRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.DirectionalLightIds"), DirectionalLightIds);

		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

		FRDGBufferRef ScreenSpaceGridBoundsRDG = nullptr;
			
		{
			// Mark coarse pages
			// NOTE: Must do this *first*. In the case where bIncludeNonNaniteGeometry is false we need to ensure that the request
			// can be over-written by any pixel pages that *do* want Non-Nanite geometry. We avoid writing with atomics since that
			// is much slower.
			// Because of this we also cannot overlap this pass with the following ones.
			bool bMarkCoarsePagesDirectional = CVarMarkCoarsePagesDirectional.GetValueOnRenderThread() != 0;
			bool bMarkCoarsePagesLocal = CVarMarkCoarsePagesLocal.GetValueOnRenderThread() != 0;
			// Note: always run this pass such that the distant lights may be marked if need be
			{
				FMarkCoarsePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FMarkCoarsePagesCS::FParameters >();
				PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
				PassParameters->OutPageRequestFlags = GraphBuilder.CreateUAV(PageRequestFlagsRDG);
				PassParameters->bMarkCoarsePagesLocal = bMarkCoarsePagesLocal ? 1 : 0;
				PassParameters->ClipmapFirstLevel = FVirtualShadowMapClipmap::GetFirstLevel();
				PassParameters->ClipmapIndexMask = bMarkCoarsePagesDirectional ? FVirtualShadowMapClipmap::GetCoarsePageClipmapIndexMask() : 0;
				PassParameters->bIncludeNonNaniteGeometry = CVarCoarsePagesIncludeNonNanite.GetValueOnRenderThread();

				auto ComputeShader = View.ShaderMap->GetShader<FMarkCoarsePagesCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("MarkCoarsePages"),
					ComputeShader,
					PassParameters,
					FIntVector(FMath::DivideAndRoundUp(uint32(GetNumShadowMaps()), FMarkCoarsePagesCS::DefaultCSGroupX), 1, 1)
				);
			}

			// Prune light grid to remove lights without VSMs
			{
				const bool bLightGridUses16BitBuffers = LightGridUses16BitBuffers(View.GetShaderPlatform());
				FRDGBufferSRVRef CulledLightDataGrid = bLightGridUses16BitBuffers ? View.ForwardLightingResources.ForwardLightData->CulledLightDataGrid16Bit : View.ForwardLightingResources.ForwardLightData->CulledLightDataGrid32Bit;
				uint32 LightGridIndexBufferSize = CulledLightDataGrid->Desc.Buffer->Desc.GetSize();
				FRDGBufferRef PrunedLightGridDataRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), LightGridIndexBufferSize), TEXT("Shadow.Virtual.LightGridData"));

				const uint32 NumLightGridCells = View.ForwardLightingResources.ForwardLightData->NumGridCells;
				FRDGBufferRef PrunedNumCulledLightsGridRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumLightGridCells), TEXT("Shadow.Virtual.NumCulledLightsGrid"));

				{
					// TODO: Make this a more dynamic bound rather than just this special case
					uint32 MinLocalLightIndex = 0;
					uint32 MaxLocalLightIndex = View.ForwardLightingResources.ForwardLightData->NumLocalLights;
					if (!bAnyLocalLightsWithVSMs)
					{
						MaxLocalLightIndex = 0;
					}

					FPruneLightGridCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FPruneLightGridCS::FParameters >();
					PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
					PassParameters->View = View.ViewUniformBuffer;
					PassParameters->ForwardLightData = View.ForwardLightingResources.ForwardLightUniformBuffer;
					PassParameters->MinLocalLightIndex = MinLocalLightIndex;
					PassParameters->MaxLocalLightIndex = MaxLocalLightIndex;
					PassParameters->OutPrunedLightGridData = GraphBuilder.CreateUAV(PrunedLightGridDataRDG);
					PassParameters->OutPrunedNumCulledLightsGrid = GraphBuilder.CreateUAV(PrunedNumCulledLightsGridRDG);
					auto ComputeShader = View.ShaderMap->GetShader<FPruneLightGridCS>();

					FComputeShaderUtils::AddPass(GraphBuilder,
						RDG_EVENT_NAME("PruneLightGrid(Min=%d,Max=%d)", MinLocalLightIndex, MaxLocalLightIndex),
						ComputeShader,
						PassParameters,
						FComputeShaderUtils::GetGroupCount(NumLightGridCells, FPruneLightGridCS::DefaultCSGroupX));
				};

				PerViewData.LightGridData = GraphBuilder.CreateSRV(PrunedLightGridDataRDG);
				PerViewData.NumCulledLightsGrid = GraphBuilder.CreateSRV(PrunedNumCulledLightsGridRDG);
				PerViewData.MaxLightGridEntryIndex = NumLightGridCells - 1u;

				// These will be used for subsequent shaders in this view loop
				// But because they will be overwritten on each loop, any other shaders that want to access
				// per-view VSM light grid data need to use the cached uniform buffers generated in UpdateCachedUniformBuffers()
				UniformParameters.PerViewData = PerViewData;
			}

			// Mark pages based on projected depth buffer pixels
			if (CVarMarkPixelPages.GetValueOnRenderThread() != 0)
			{
				// It's currently safe to overlap these passes that all write to same page request flags
				FRDGBufferUAVRef PageRequestFlagsUAV = GraphBuilder.CreateUAV(PageRequestFlagsRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);

				FBaseGeneratePageFlagsParameters BaseParameters;
				BaseParameters.VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
				BaseParameters.SceneTexturesStruct = SceneTextures.UniformBuffer;
				BaseParameters.View = View.ViewUniformBuffer;
				BaseParameters.OutPageRequestFlags = PageRequestFlagsUAV;
				BaseParameters.ForwardLightData = View.ForwardLightingResources.ForwardLightUniformBuffer;
				BaseParameters.DirectionalLightIds = GraphBuilder.CreateSRV(DirectionalLightIdsRDG);
				BaseParameters.NumDirectionalLightSmInds = uint32(DirectionalLightIds.Num());
				BaseParameters.PageDilationBorderSizeLocal = CVarPageDilationBorderSizeLocal.GetValueOnRenderThread();
				BaseParameters.PageDilationBorderSizeDirectional = CVarPageDilationBorderSizeDirectional.GetValueOnRenderThread();
				BaseParameters.bCullBackfacingPixels = ShouldCullBackfacingPixels() ? 1 : 0;
				BaseParameters.Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
				BaseParameters.MipModeLocal = CVarMarkPixelPagesMipModeLocal.GetValueOnRenderThread();

				auto GeneratePageFlags = [&](const EVirtualShadowMapProjectionInputType InputType)
				{
					FGeneratePageFlagsFromPixelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FGeneratePageFlagsFromPixelsCS::FParameters >();
					PassParameters->Base = BaseParameters;

					const FIntPoint PixelStride(
						FMath::Clamp(CVarVirtualShadowMapPageMarkingPixelStrideX.GetValueOnRenderThread(), 1, 128),
						FMath::Clamp(CVarVirtualShadowMapPageMarkingPixelStrideY.GetValueOnRenderThread(), 1, 128));
					
					// If Lumen has valid front layer history data use it, otherwise use same frame front layer depth
					bool bFrontLayerEnabled = false;
					if (IsVSMTranslucentHighQualityEnabled())
					{
						if (FrontLayerTranslucencyData.IsValid())
						{
							PassParameters->FrontLayerMode = 0;
							PassParameters->FrontLayerTranslucencyDepthTexture = FrontLayerTranslucencyData.SceneDepth;
							PassParameters->FrontLayerTranslucencyNormalTexture = FrontLayerTranslucencyData.Normal;
							bFrontLayerEnabled = true;
						}
						else if (IsLumenFrontLayerHistoryValid(View))
						{
							const FReflectionTemporalState& State = View.ViewState->Lumen.TranslucentReflectionState;
							const FIntPoint HistoryResolution = State.LayerSceneDepthHistory->GetDesc().Extent;
							const FVector2f InvBufferSize(1.0f / SceneTextures.Config.Extent.X, 1.0f / SceneTextures.Config.Extent.Y);
							PassParameters->FrontLayerMode = 1;
							PassParameters->FrontLayerHistoryUVMinMax = FVector4f(
								(State.HistoryViewRect.Min.X + 0.5f) * InvBufferSize.X,
								(State.HistoryViewRect.Min.Y + 0.5f) * InvBufferSize.Y,
								(State.HistoryViewRect.Max.X - 0.5f) * InvBufferSize.X,
								(State.HistoryViewRect.Max.Y - 0.5f) * InvBufferSize.Y);
							PassParameters->FrontLayerHistoryScreenPositionScaleBias = State.HistoryScreenPositionScaleBias;
							PassParameters->FrontLayerHistoryBufferSizeAndInvSize = FVector4f(HistoryResolution.X, HistoryResolution.Y, 1.f/HistoryResolution.X, 1.f/HistoryResolution.Y);
							PassParameters->FrontLayerTranslucencyDepthTexture = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.TranslucentReflectionState.LayerSceneDepthHistory, TEXT("VSM.FrontLayerHistoryDepth"));
							PassParameters->FrontLayerTranslucencyNormalTexture = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.TranslucentReflectionState.LayerSceneNormalHistory, TEXT("VSM.FrontLayerHistoryNormal"));
							bFrontLayerEnabled = true;
						}
					}

					PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
					bool bWaterEnabled = false;
					if (SingleLayerWaterPrePassResult && InputType == EVirtualShadowMapProjectionInputType::GBuffer)
					{
						PassParameters->SingleLayerWaterDepthTexture = SingleLayerWaterPrePassResult->DepthPrepassTexture.Resolve;
						PassParameters->SingleLayerWaterTileMask = GraphBuilder.CreateSRV(SingleLayerWaterPrePassResult->ViewTileClassification[ViewIndex].TileMaskBuffer != nullptr ?
							SingleLayerWaterPrePassResult->ViewTileClassification[ViewIndex].TileMaskBuffer :
							GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(uint32), 0xFFFFFFFF));
						PassParameters->SingleLayerWaterTileViewRes = SingleLayerWaterPrePassResult->ViewTileClassification[ViewIndex].TiledViewRes;
						bWaterEnabled = true;
					}
					PassParameters->PixelStride = PixelStride;
					
					const FIntPoint StridedPixelSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), PixelStride);
					// Note: we use the tile size defined by the water as the group-size - this is needed because the tile mask testing code relies on the size being the same to scalarize efficiently.
					const FIntPoint GridSize = FIntPoint::DivideAndRoundUp(StridedPixelSize, SLW_TILE_SIZE_XY);

					if (InputType == EVirtualShadowMapProjectionInputType::HairStrands)
					{
						FGeneratePageFlagsFromPixelsCS::FPermutationDomain PermutationVector;
						PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FInputType>(static_cast<uint32>(InputType));
						auto ComputeShader = View.ShaderMap->GetShader<FGeneratePageFlagsFromPixelsCS>(PermutationVector);

						check(View.HairStrandsViewData.VisibilityData.TileData.IsValid());
						PassParameters->IndirectBufferArgs = View.HairStrandsViewData.VisibilityData.TileData.TileIndirectDispatchBuffer;
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("GeneratePageFlagsFromPixels(HairStrands,Tile)"),
							ComputeShader,
							PassParameters,
							View.HairStrandsViewData.VisibilityData.TileData.TileIndirectDispatchBuffer,
							View.HairStrandsViewData.VisibilityData.TileData.GetIndirectDispatchArgOffset(FHairStrandsTiles::ETileType::HairAll));
					}
					else
					{						
						FGeneratePageFlagsFromPixelsCS::FPermutationDomain PermutationVector;
						PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FInputType>(static_cast<uint32>(InputType));
						PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FWaterDepth>(bWaterEnabled);
						PermutationVector.Set<FGeneratePageFlagsFromPixelsCS::FTranslucencyDepth>(bFrontLayerEnabled);
						auto ComputeShader = View.ShaderMap->GetShader<FGeneratePageFlagsFromPixelsCS>(PermutationVector);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("GeneratePageFlagsFromPixels(%s,%s%sNumShadowMaps=%d,{%d,%d})", ToString(InputType), (bWaterEnabled ? TEXT("Water,") : TEXT("")), (bFrontLayerEnabled ? TEXT("FrontLayer,") : TEXT("")), GetNumFullShadowMaps(), GridSize.X, GridSize.Y),
							ComputeShader,
							PassParameters,
							FIntVector(GridSize.X, GridSize.Y, 1));
					}
				};
				if (FroxelRenderer.IsEnabled() && CVarMarkPagesUseFroxels.GetValueOnRenderThread() != 0)
				{
					auto AddPass_FroxelBuild = [&](const Froxel::FViewData* ViewFroxelData, int32 PassId, bool bShouldMarkLocaLights)
					{
						if (ViewFroxelData == nullptr)
						{
							return;
						}

						FGeneratePageFlagsFromFroxelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FGeneratePageFlagsFromFroxelsCS::FParameters >();
						PassParameters->Base = BaseParameters;
						PassParameters->PassId = PassId;
						PassParameters->bShouldMarkLocaLights = bShouldMarkLocaLights ? 1u : 0u;
						PassParameters->DebugRange = CVarDebugDrawFroxelRange.GetValueOnRenderThread();

						PassParameters->FroxelParameters = ViewFroxelData->GetShaderParameters(GraphBuilder);
						PassParameters->IndirectBufferArgs = ViewFroxelData->FroxelArgsRDG;

						FGeneratePageFlagsFromFroxelsCS::FPermutationDomain PermutationVector;
						bool bDebugRender = CVarDebugDrawFroxels.GetValueOnRenderThread() < 0 || CVarDebugDrawFroxels.GetValueOnRenderThread() == (PassId + 1);
						if (bDebugRender)
						{
							PermutationVector.Set<FGeneratePageFlagsFromFroxelsCS::FDebugRenderDim>(true);
							ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintStruct);
						}

						auto ComputeShader = View.ShaderMap->GetShader<FGeneratePageFlagsFromFroxelsCS>(PermutationVector);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("GeneratePageFlagsFromFroxels(NumShadowMaps=%d)", GetNumFullShadowMaps()),
							ComputeShader,
							PassParameters,
							ViewFroxelData->FroxelArgsRDG,
							ViewFroxelData->ArgsOffset);
					};

					AddPass_FroxelBuild(FroxelRenderer.GetView(ViewIndex), 0, true);
					if (SingleLayerWaterPrePassResult && SingleLayerWaterPrePassResult->Froxels.GetView(ViewIndex))
					{
						// Not marking local lights since SLW does not support sampling shadow for these.
						AddPass_FroxelBuild(SingleLayerWaterPrePassResult->Froxels.GetView(ViewIndex), 1, false);
					}
				}
				else
				{
					GeneratePageFlags(EVirtualShadowMapProjectionInputType::GBuffer);
				}
				if (HairStrands::HasViewHairStrandsData(View))
				{
					GeneratePageFlags(EVirtualShadowMapProjectionInputType::HairStrands);
				}
			}
		}
	}

	// NOTE: Could move this into a single allocation...? No guarantee they will always remain uint32s though
	PageTableRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlagsToAllocate), TEXT("Shadow.Virtual.PageTable"));		
	PageFlagsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumPageFlagsToAllocate), TEXT("Shadow.Virtual.PageFlags"));

	AddClearPageTableUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageTableRDG), 0);
	AddClearPageTableUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PageFlagsRDG), 0);

	// Update cached or newly invalidated pages with respect to the new requests
	{	
		// Cached data from previous frames is available and valid.  Note that we currently don't support GPUMask varying within
		// a view family, so just use the first view's GPU mask.
		const bool bCacheDataAvailable = CacheManager->IsCacheDataAvailable();
		const bool bCacheDataValid = bCacheDataAvailable && CacheManager->GetCacheValidGPUMask().ContainsAll(Views[0].GPUMask);

		FUpdatePhysicalPages::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdatePhysicalPages::FParameters>();
		PassParameters->VirtualShadowMap		= GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
		PassParameters->OutPhysicalPageLists	= GraphBuilder.CreateUAV(PhysicalPageListsRDG);

		if (bCacheDataValid)
		{
			PassParameters->PageRequestFlags		   = GraphBuilder.CreateSRV(PageRequestFlagsRDG);
			PassParameters->OutPageTable			   = GraphBuilder.CreateUAV(PageTableRDG);
			PassParameters->OutPageFlags			   = GraphBuilder.CreateUAV(PageFlagsRDG);
			PassParameters->PrevPhysicalPageLists      = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(CacheManager->GetPrevBuffers().PhysicalPageLists));
			PassParameters->MaxPageAgeSinceLastRequest = GVSMMaxPageAgeSinceLastRequest;
			PassParameters->bDynamicPageInvalidation   = 1;
#if !UE_BUILD_SHIPPING
			PassParameters->bDynamicPageInvalidation   = CVarDebugSkipDynamicPageInvalidation.GetValueOnRenderThread() == 0 ? 1 : 0;
#endif
			PassParameters->bAllocateViaLRU			   = CVarCacheAllocateViaLRU.GetValueOnRenderThread();
		}

		FUpdatePhysicalPages::FPermutationDomain PermutationVector;
		PermutationVector.Set<FUpdatePhysicalPages::FHasCacheDataDim>(bCacheDataValid);
		SetStatsArgsAndPermutation<FUpdatePhysicalPages>(ShouldGenerateStats(), StatsBufferUAV, PassParameters, PermutationVector);
		auto ComputeShader = Views[0].ShaderMap->GetShader<FUpdatePhysicalPages>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("UpdatePhysicalPages"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FUpdatePhysicalPages::DefaultCSGroupX), 1, 1)
		);

#if WITH_MGPU
		// Need to propagate previous physical page lists for GPUs we didn't update in the above pass.  This is only a
		// 64 KB structure, so copying it is cheap.  Ping ponging and independently tracking which is the current buffer
		// separately per GPU is another option, which avoids the copy, but adds a bunch of complexity.
		FRHIGPUMask InverseGPUMask;
		if (CacheManager->GetPrevBuffers().PhysicalPageLists && Views[0].GPUMask.Invert(InverseGPUMask) && InverseGPUMask.Intersects(CacheManager->GetCacheValidGPUMask()))
		{
			RDG_GPU_MASK_SCOPE(GraphBuilder, InverseGPUMask);
			AddCopyBufferPass(GraphBuilder, PhysicalPageListsRDG, GraphBuilder.RegisterExternalBuffer(CacheManager->GetPrevBuffers().PhysicalPageLists));
		}

		// Track which GPUs the cache has been initialized on.  Merges GPU mask if cache data was already available, otherwise sets mask to initialize it.
		CacheManager->UpdateCacheValidGPUMask(Views[0].GPUMask, bCacheDataAvailable);
#endif
	}

	{
		FPackAvailablePagesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPackAvailablePagesCS::FParameters>();
		PassParameters->VirtualShadowMap		= GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutPhysicalPageLists	= GraphBuilder.CreateUAV(PhysicalPageListsRDG);						
		auto ComputeShader = Views[0].ShaderMap->GetShader<FPackAvailablePagesCS>();

		// NOTE: We run a single CS group here (see shader)
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PackAvailablePages"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}

	// Add any newly empty pages to the list of available pages to allocate
	// We add them at the end so that they take priority over any pages with valid cached data
	AppendPhysicalPageList(GraphBuilder, true);

	{
		FAllocateNewPageMappingsCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FAllocateNewPageMappingsCS::FParameters >();
		PassParameters->VirtualShadowMap		= GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->PageRequestFlags		= GraphBuilder.CreateSRV(PageRequestFlagsRDG);
		PassParameters->OutPageTable			= GraphBuilder.CreateUAV(PageTableRDG);
		PassParameters->OutPageFlags			= GraphBuilder.CreateUAV(PageFlagsRDG);
		PassParameters->OutPhysicalPageLists	= GraphBuilder.CreateUAV(PhysicalPageListsRDG);
		PassParameters->OutPhysicalPageMetaData	= GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);

		FAllocateNewPageMappingsCS::FPermutationDomain PermutationVector;
		SetStatsArgsAndPermutation<FAllocateNewPageMappingsCS>(ShouldGenerateStats(), StatsBufferUAV, PassParameters, PermutationVector);
		auto ComputeShader = Views[0].ShaderMap->GetShader<FAllocateNewPageMappingsCS>(PermutationVector);

		PerPageShaderDispatcher.AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("AllocateNewPageMappings"),
			ComputeShader,
			PassParameters
		);
	}

	{
		// Run pass building hierarchical page flags to make culling acceptable performance.
		FGenerateHierarchicalPageFlagsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGenerateHierarchicalPageFlagsCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
		PassParameters->OutPageFlags = GraphBuilder.CreateUAV(PageFlagsRDG);
		PassParameters->OutUncachedPageRectBounds = GraphBuilder.CreateUAV(UncachedPageRectBoundsRDG);
		PassParameters->OutAllocatedPageRectBounds = GraphBuilder.CreateUAV(AllocatedPageRectBoundsRDG);

		auto ComputeShader = Views[0].ShaderMap->GetShader<FGenerateHierarchicalPageFlagsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateHierarchicalPageFlags"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(GetMaxPhysicalPages(), FGenerateHierarchicalPageFlagsCS::DefaultCSGroupX)
		);
	}

	// NOTE: We could skip this (in shader) for shadow maps that only have 1 mip (ex. clipmaps)
	if (GetNumFullShadowMaps() > 0)
	{
		// Propagate mapped mips down the hierarchy to allow O(1) lookup of coarser mapped pages
		FPropagateMappedMipsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPropagateMappedMipsCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutPageTable	= GraphBuilder.CreateUAV(PageTableRDG);

		auto ComputeShader = Views[0].ShaderMap->GetShader<FPropagateMappedMipsCS>();
		PerPageShaderDispatcher.AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("PropagateMappedMips"),
			ComputeShader,
			PassParameters
		);
	}

	// Initialize the physical page pool
	check(PhysicalPagePoolRDG != nullptr);
	{
		RDG_EVENT_SCOPE( GraphBuilder, "InitializePhysicalPages" );
		
		// Note: We use GetTotalAllocatedPhysicalPages() to size the buffer as the selection shader emits both static/dynamic pages separately when enabled.
		FRDGBufferRef PhysicalPagesToInitializeRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetTotalAllocatedPhysicalPages() + 1), TEXT("Shadow.Virtual.PhysicalPagesToInitialize"));

		// 2. Filter the relevant physical pages and set up the indirect args
		{
			FSelectPagesToInitializeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectPagesToInitializeCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
			PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
			PassParameters->OutInitializePagesIndirectArgsBuffer = GraphBuilder.CreateUAV(InitializePagesIndirectArgsRDG);
			PassParameters->OutPhysicalPagesToInitialize = GraphBuilder.CreateUAV(PhysicalPagesToInitializeRDG);
			FSelectPagesToInitializeCS::FPermutationDomain PermutationVector;
			SetStatsArgsAndPermutation<FSelectPagesToInitializeCS>(ShouldGenerateStats(), StatsBufferUAV, PassParameters, PermutationVector);

			auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FSelectPagesToInitializeCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SelectPagesToInitialize"),
				ComputeShader,
				PassParameters,
				FIntVector(FMath::DivideAndRoundUp(GetMaxPhysicalPages(), FSelectPagesToInitializeCS::DefaultCSGroupX), 1, 1)
			);

		}
		// 3. Indirect dispatch to clear the selected pages
		{
			FInitializePhysicalPagesIndirectCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializePhysicalPagesIndirectCS::FParameters>();
			PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
			PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);
			PassParameters->OutPhysicalPagePool = GraphBuilder.CreateUAV(PhysicalPagePoolRDG);
			PassParameters->IndirectArgs = InitializePagesIndirectArgsRDG;
			PassParameters->PhysicalPagesToInitialize = GraphBuilder.CreateSRV(PhysicalPagesToInitializeRDG);
			auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FInitializePhysicalPagesIndirectCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("InitializePhysicalMemoryIndirect"),
				ComputeShader,
				PassParameters,
				PassParameters->IndirectArgs,
				0
			);
		}
	}

	// If present, we always clear the entire third slice of the array as that is used for visualization for the current render
	// TODO: There are potentially interesting cases where we allow the visualization to live along with cached data as well, but
	// for current performance debug purposes this is more directly in line with the cost of that page on a given frame.
	if (PhysicalPagePoolRDG->Desc.ArraySize >= 3)
	{
		// Clear only array slice 2
		FRDGTextureUAVDesc Desc(PhysicalPagePoolRDG, 0 /* MipLevel */, PF_Unknown, 2, 1);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Desc), 0U);
	}

	UniformParameters.PageTable = GraphBuilder.CreateSRV(PageTableRDG);
	UniformParameters.PageFlags = GraphBuilder.CreateSRV(PageFlagsRDG);
	UniformParameters.AllocatedPageRectBounds = GraphBuilder.CreateSRV(AllocatedPageRectBoundsRDG);
	UniformParameters.UncachedPageRectBounds = GraphBuilder.CreateSRV(UncachedPageRectBoundsRDG);

	// Add pass to pipe back important stats
	{
		FVirtualSmFeedbackStatusCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmFeedbackStatusCS::FParameters>();
		PassParameters->PhysicalPageLists = GraphBuilder.CreateSRV(PhysicalPageListsRDG);
		PassParameters->GPUMessageParams = GPUMessage::GetShaderParameters(GraphBuilder);
		PassParameters->StatusMessageId = CacheManager->GetStatusFeedbackMessageId();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);

		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FVirtualSmFeedbackStatusCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Feedback Status"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1)
		);
	}

	// Put any remaining available pages back into the sorted list for next frame
	// NOTE: Must do this *after* feedback status pass
	AppendPhysicalPageList(GraphBuilder, false);

	UpdateCachedUniformBuffers(GraphBuilder);

#if !UE_BUILD_SHIPPING
	// Only dump one frame of light data
	GDumpVSMLightNames = false;
#endif
}

class FDebugVisualizeVirtualSmCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FDebugVisualizeVirtualSmCS);
	SHADER_USE_PARAMETER_STRUCT(FDebugVisualizeVirtualSmCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, ProjectionParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER(uint32, DebugTargetWidth)
		SHADER_PARAMETER(uint32, DebugTargetHeight)
		SHADER_PARAMETER(uint32, BorderWidth)
		SHADER_PARAMETER(uint32, VisualizeModeId)
		SHADER_PARAMETER(int32, VirtualShadowMapId)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutVisualize)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDebugVisualizeVirtualSmCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapDebug.usf", "DebugVisualizeVirtualSmCS", SF_Compute);


void FVirtualShadowMapArray::RenderDebugInfo(FRDGBuilder& GraphBuilder, TArrayView<FViewInfo> Views)
{
	check(IsEnabled());
			
	if (Views.Num() > 0)
	{
		LogStats(GraphBuilder, Views[0]);
	}

	if (DebugVisualizationOutput.IsEmpty() || VisualizeLight.IsEmpty())
	{
		return;
	}

	const FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
	if (VisualizationData.GetActiveModeID() != VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_VIRTUAL_SPACE)
	{
		return;
	}

	int32 BorderWidth = 2;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (!VisualizeLight[ViewIndex].IsValid())
		{
			continue;
		}

		FViewInfo& View = Views[ViewIndex];

		FIntPoint DebugTargetExtent = DebugVisualizationOutput[ViewIndex]->Desc.Extent;

		FDebugVisualizeVirtualSmCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDebugVisualizeVirtualSmCS::FParameters>();
		PassParameters->ProjectionParameters = GetSamplingParameters(GraphBuilder, ViewIndex);
		PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);

		PassParameters->DebugTargetWidth = DebugTargetExtent.X;
		PassParameters->DebugTargetHeight = DebugTargetExtent.Y;
		PassParameters->BorderWidth = BorderWidth;
		PassParameters->VisualizeModeId = VisualizationData.GetActiveModeID();
		PassParameters->VirtualShadowMapId = VisualizeLight[ViewIndex].GetVirtualShadowMapId();

		PassParameters->OutVisualize = GraphBuilder.CreateUAV(DebugVisualizationOutput[ViewIndex]);

		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FDebugVisualizeVirtualSmCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DebugVisualizeVirtualShadowMap"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(DebugTargetExtent, FVirtualShadowMapPageManagementShader::DefaultCSGroupXY)
		);
	}
}


class FVirtualSmLogStatsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmLogStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmLogStatsCS, FVirtualShadowMapPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS"); 
	using FPermutationDomain = TShaderPermutationDomain<FGenerateStatsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_INCLUDE(GPUMessage::FParameters, GPUMessageParams)
		SHADER_PARAMETER_STRUCT_INCLUDE( ShaderPrint::FShaderParameters, ShaderPrintStruct )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InStatsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FNaniteStats>, NaniteStats)
		SHADER_PARAMETER(int, ShowStatsValue)
		SHADER_PARAMETER(uint32, StatusMessageId)
		SHADER_PARAMETER(uint32, StatsMessageId)
	END_SHADER_PARAMETER_STRUCT()


	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualShadowMapPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// Disable optimizations as shader print causes long compile times
		OutEnvironment.CompilerFlags.Add(CFLAG_SkipOptimizations);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmLogStatsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPrintStats.usf", "LogVirtualSmStatsCS", SF_Compute);

void FVirtualShadowMapArray::LogStats(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	check(IsEnabled());
	LLM_SCOPE_BYTAG(Nanite);

	if (!StatsBufferRDG)
	{
		return;
	}

	FVirtualSmLogStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmLogStatsCS::FParameters>();
	PassParameters->InStatsBuffer = GraphBuilder.CreateSRV(StatsBufferRDG);
	PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
	PassParameters->GPUMessageParams = GPUMessage::GetShaderParameters(GraphBuilder);
	PassParameters->StatusMessageId = CacheManager->GetStatusFeedbackMessageId();

	// If ShouldGenerateStats() is false, the stats buffer will only have data needed for Status messages (e.g. for overflow tracking)
	bool bGenerateStats = ShouldGenerateStats();

	FVirtualSmLogStatsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FVirtualSmLogStatsCS::FGenerateStatsDim>(bGenerateStats);
	
	if (bGenerateStats)
	{
		// Convenience, enable shader print automatically
		ShaderPrint::SetEnabled(true);

		ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintStruct);

		int ShowStatsValue = CVarShowStats.GetValueOnRenderThread();
		PassParameters->ShowStatsValue = ShowStatsValue;

#if !UE_BUILD_SHIPPING
		PassParameters->StatsMessageId = CacheManager->GetStatsFeedbackMessageId();
		bool bBindNaniteStatsBuffer = StatsNaniteBufferRDG != nullptr;
#else
		PassParameters->StatsMessageId = INDEX_NONE;
		constexpr bool bBindNaniteStatsBuffer = false;
#endif

		if (bBindNaniteStatsBuffer)
		{
			PassParameters->NaniteStats = GraphBuilder.CreateSRV(StatsNaniteBufferRDG);
		}
		else
		{
			PassParameters->NaniteStats = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FNaniteStats)));
		}
	}
	
	auto ComputeShader = View.ShaderMap->GetShader<FVirtualSmLogStatsCS>(PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("VSM Log Stats And Status"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1)
	);
}


class FVirtualSmLogPageListStatsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmLogPageListStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmLogPageListStatsCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintStruct)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< int >, OutPhysicalPageLists)
		SHADER_PARAMETER(int, PageListStatsRow)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVirtualShadowMapPageManagementShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// Disable optimizations as shader print causes long compile times
		OutEnvironment.CompilerFlags.Add(CFLAG_SkipOptimizations);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmLogPageListStatsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "LogPageListStatsCS", SF_Compute);


void FVirtualShadowMapArray::CreateMipViews( TArray<Nanite::FPackedView, SceneRenderingAllocator>& Views ) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateMipViews);

	// strategy: 
	// 1. Use the cull pass to generate copies of every node for every view needed.
	// [2. Fabricate a HZB array?]
	
	const int32 NumPrimaryViews = Views.Num();

	const float NaniteMaxPixelsPerEdge   = CVarNaniteMaxPixelsPerEdge.GetValueOnRenderThread();
	const float NaniteMinPixelsPerEdgeHW = CVarNaniteMinPixelsPerEdgeHW.GetValueOnRenderThread();

	// 1. create derivative views for each of the Mip levels, 
	Views.AddDefaulted( NumPrimaryViews * ( FVirtualShadowMap::MaxMipLevels - 1) );

	int32 MaxMips = 0;
	for (int32 ViewIndex = 0; ViewIndex < NumPrimaryViews; ++ViewIndex)
	{
		const Nanite::FPackedView& PrimaryView = Views[ViewIndex];
		
		ensure( PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X >= 0 && PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.X < GetNumShadowMapSlots() );
		ensure( PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y == 0 );
		ensure( PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z > 0 && PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z <= FVirtualShadowMap::MaxMipLevels );
		
		const int32 NumMips = PrimaryView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z;
		MaxMips = FMath::Max(MaxMips, NumMips);
		for (int32 MipLevel = 0; MipLevel < NumMips; ++MipLevel)
		{
			Nanite::FPackedView& MipView = Views[ MipLevel * NumPrimaryViews + ViewIndex ];	// Primary (Non-Mip views) first followed by derived mip views.

			if( MipLevel > 0 )
			{
				MipView = PrimaryView;

				// Slightly messy, but extract any scale factor that was applied to the LOD scale for re-application below
				MipView.UpdateLODScales(NaniteMaxPixelsPerEdge, NaniteMinPixelsPerEdgeHW);
				float LODScaleFactor = PrimaryView.LODScales.X / MipView.LODScales.X;

				MipView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Y = MipLevel;
				MipView.TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ.Z = NumMips;	//FVirtualShadowMap::MaxMipLevels;

				// Size of view, for the virtual SMs these are assumed to not be offset.
				FIntPoint ViewSize = FIntPoint::DivideAndRoundUp( FIntPoint( PrimaryView.ViewSizeAndInvSize.X + 0.5f, PrimaryView.ViewSizeAndInvSize.Y + 0.5f ), 1U <<  MipLevel );
				FIntPoint ViewMin = FIntPoint(MipView.ViewRect.X, MipView.ViewRect.Y) / (1U <<  MipLevel);

				MipView.ViewSizeAndInvSize = FVector4f(ViewSize.X, ViewSize.Y, 1.0f / float(ViewSize.X), 1.0f / float(ViewSize.Y));
				MipView.ViewRect = FIntVector4(ViewMin.X, ViewMin.Y, ViewMin.X + ViewSize.X, ViewMin.Y + ViewSize.Y);

				MipView.UpdateLODScales(NaniteMaxPixelsPerEdge, NaniteMinPixelsPerEdgeHW);
				MipView.LODScales.X *= LODScaleFactor;
			}

			MipView.HZBTestViewRect = MipView.ViewRect;	// Assumed to always be the same for VSM

			float RcpExtXY = 1.0f / ( FVirtualShadowMap::PageSize * FVirtualShadowMap::RasterWindowPages );

			// Transform clip from virtual address space to viewport.
			MipView.ClipSpaceScaleOffset = FVector4f(
				  MipView.ViewSizeAndInvSize.X * RcpExtXY,
				  MipView.ViewSizeAndInvSize.Y * RcpExtXY,
				 (MipView.ViewSizeAndInvSize.X + 2.0f * MipView.ViewRect.X) * RcpExtXY - 1.0f,
				-(MipView.ViewSizeAndInvSize.Y + 2.0f * MipView.ViewRect.Y) * RcpExtXY + 1.0f);


			// Set streaming priority category to zero for some reason
			MipView.StreamingPriorityCategory_AndFlags &= ~uint32(NANITE_STREAMING_PRIORITY_CATEGORY_MASK);
		}
	}

	// Remove unused mip views
	check(Views.IsEmpty() || MaxMips > 0);
	Views.SetNum(MaxMips * NumPrimaryViews, EAllowShrinking::No);
}


class FVirtualSmPrintClipmapStatsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmPrintClipmapStatsCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmPrintClipmapStatsCS, FVirtualShadowMapPageManagementShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		//SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintStruct)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FIntVector4 >, AllocatedPageRectBounds)
		SHADER_PARAMETER(uint32, ShadowMapIdRangeStart)
		SHADER_PARAMETER(uint32, ShadowMapIdRangeEnd)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmPrintClipmapStatsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPrintStats.usf", "PrintClipmapStats", SF_Compute);


BEGIN_SHADER_PARAMETER_STRUCT(FVirtualShadowDepthPassParameters,)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FShadowDepthPassUniformParameters, ShadowDepthPass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FCullPerPageDrawCommandsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullPerPageDrawCommandsCs);
	SHADER_USE_PARAMETER_STRUCT(FCullPerPageDrawCommandsCs, FGlobalShader)

	class FUseHzbDim : SHADER_PERMUTATION_BOOL("USE_HZB_OCCLUSION");
	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	class FBatchedDim : SHADER_PERMUTATION_BOOL("ENABLE_BATCH_MODE");
	using FPermutationDomain = TShaderPermutationDomain< FUseHzbDim, FBatchedDim, FGenerateStatsDim >;

	static constexpr uint32 ThreadGroupSize = FInstanceProcessingGPULoadBalancer::ThreadGroupSize;

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportVirtualShadowMaps(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FHZBShaderParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, HZBPageTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, HZBPageFlags)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint4 >, HZBPageRectBounds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, HZBTextureArray)
		SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
		SHADER_PARAMETER(FVector2f, HZBSize)
		SHADER_PARAMETER(uint32, HZBMode)
	END_SHADER_PARAMETER_STRUCT()



	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)

		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutDirtyPageFlags)

		SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceProcessingGPULoadBalancer::FShaderParameters, LoadBalancerParameters)

		SHADER_PARAMETER(int32, FirstPrimaryView)
		SHADER_PARAMETER(int32, NumPrimaryViews)
		SHADER_PARAMETER(uint32, TotalPrimaryViews)
		SHADER_PARAMETER(uint32, VisibleInstancesBufferNum)
		SHADER_PARAMETER(int32, DynamicInstanceIdOffset)
		SHADER_PARAMETER(int32, DynamicInstanceIdMax)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FPackedView >, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, DrawCommandDescs)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FContextBatchInfo >, BatchInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVSMCullingBatchInfo >, VSMCullingBatchInfos)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint32 >, BatchInds)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FVSMVisibleInstanceCmd>, VisibleInstancesOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawIndirectArgsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, VisibleInstanceCountBufferOut)

		SHADER_PARAMETER_STRUCT_INCLUDE(FHZBShaderParameters, HZBShaderParameters)

		SHADER_PARAMETER(uint32, NumPageAreaDiagnosticSlots)
		SHADER_PARAMETER(uint32, LargeInstancePageAreaThreshold)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCullPerPageDrawCommandsCs, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapBuildPerPageDrawCommands.usf", "CullPerPageDrawCommandsCs", SF_Compute);



class FAllocateCommandInstanceOutputSpaceCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FAllocateCommandInstanceOutputSpaceCs);
	SHADER_USE_PARAMETER_STRUCT(FAllocateCommandInstanceOutputSpaceCs, FGlobalShader)
public:
	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportVirtualShadowMaps(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DrawIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, InstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TmpInstanceIdOffsetBufferOut)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FAllocateCommandInstanceOutputSpaceCs, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapBuildPerPageDrawCommands.usf", "AllocateCommandInstanceOutputSpaceCs", SF_Compute);


class FOutputCommandInstanceListsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOutputCommandInstanceListsCs);
	SHADER_USE_PARAMETER_STRUCT(FOutputCommandInstanceListsCs, FGlobalShader)
public:
	static constexpr int32 NumThreadsPerGroup = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			DoesPlatformSupportVirtualShadowMaps(Parameters.Platform);
	}

	/**
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FInstanceProcessingGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("NUM_THREADS_PER_GROUP"), NumThreadsPerGroup);
		OutEnvironment.SetDefine(TEXT("NANITE_MULTI_VIEW"), 1);
		OutEnvironment.SetDefine(TEXT("INDIRECT_ARGS_NUM_WORDS"), FInstanceCullingContext::IndirectArgsNumWords);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< FVSMVisibleInstanceCmd >, VisibleInstances)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, InstanceIdsBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PageInfoBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TmpInstanceIdOffsetBufferOut)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleInstanceCountBuffer)

		// Needed reference for make RDG happy somehow
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FOutputCommandInstanceListsCs, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapBuildPerPageDrawCommands.usf", "OutputCommandInstanceListsCs", SF_Compute);

class FUpdateAndClearDirtyFlagsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FUpdateAndClearDirtyFlagsCS);
	SHADER_USE_PARAMETER_STRUCT(FUpdateAndClearDirtyFlagsCS, FVirtualShadowMapPageManagementShader)

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, DirtyPageFlagsInOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< FPhysicalPageMetaData >, OutPhysicalPageMetaData)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FUpdateAndClearDirtyFlagsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "UpdateAndClearDirtyFlagsCS", SF_Compute);


struct FCullingResult
{
	FRDGBufferRef DrawIndirectArgsRDG;
	FRDGBufferRef InstanceIdOffsetBufferRDG;
	FRDGBufferRef InstanceIdsBuffer;
	FRDGBufferRef PageInfoBuffer;
	uint32 MaxNumInstancesPerPass;
};

template <typename InstanceCullingLoadBalancerType>
static FCullingResult AddCullingPasses(FRDGBuilder& GraphBuilder,
	const TConstArrayView<FRHIDrawIndexedIndirectParameters> &IndirectArgs,
	const TConstArrayView<uint32>& DrawCommandDescs,
	const TConstArrayView<uint32>& InstanceIdOffsets,
	InstanceCullingLoadBalancerType *LoadBalancer,
	const TConstArrayView<FInstanceCullingMergedContext::FContextBatchInfoPacked> BatchInfos,
	const TConstArrayView<FVSMCullingBatchInfo> VSMCullingBatchInfos,
	const TConstArrayView<uint32> BatchInds,
	uint32 TotalInstances,
	uint32 TotalViewScaledInstanceCount,
	uint32 TotalPrimaryViews,
	FRDGBufferRef VirtualShadowViewsRDG,
	const FCullPerPageDrawCommandsCs::FHZBShaderParameters &HZBShaderParameters,
	FVirtualShadowMapArray &VirtualShadowMapArray,
	FSceneUniformBuffer& SceneUniformBuffer,
	ERHIFeatureLevel::Type FeatureLevel)
{
	const bool bUseBatchMode = !BatchInds.IsEmpty();

	int32 NumIndirectArgs = IndirectArgs.Num();

	FRDGBufferRef TmpInstanceIdOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.TmpInstanceIdOffsetBuffer"), sizeof(uint32), NumIndirectArgs, nullptr, 0);

	FCullingResult CullingResult;
	// TotalViewScaledInstanceCount is conservative since it is the number of instances needed if each instance was drawn into every possible mip-level.
	// This is far more than we'd expect in reasonable circumstances, so we use a scale factor to reduce memory pressure from these passes.
	const uint32 MaxCulledInstanceCount = uint32(CVarNonNaniteMaxCulledInstanceAllocationSize.GetValueOnRenderThread());
	const uint32 ScaledInstanceCount = static_cast<uint32>(static_cast<double>(TotalViewScaledInstanceCount) * CVarNonNaniteCulledInstanceAllocationFactor.GetValueOnRenderThread());
	ensureMsgf(ScaledInstanceCount <= MaxCulledInstanceCount, TEXT("Possible non-nanite VSM Instance culling overflow detected (esitmated required size: %d, if visual artifacts appear either increase the r.Shadow.Virtual.NonNanite.MaxCulledInstanceAllocationSize (%d) or reduce r.Shadow.Virtual.NonNanite.CulledInstanceAllocationFactor (%.2f)"), ScaledInstanceCount, MaxCulledInstanceCount, CVarNonNaniteCulledInstanceAllocationFactor.GetValueOnRenderThread());
	CullingResult.MaxNumInstancesPerPass = FMath::Clamp(ScaledInstanceCount, 1u, MaxCulledInstanceCount);

	FRDGBufferRef VisibleInstancesRdg = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VisibleInstances"), sizeof(FVSMVisibleInstanceCmd), CullingResult.MaxNumInstancesPerPass, nullptr, 0);

	FRDGBufferRef VisibleInstanceWriteOffsetRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VisibleInstanceWriteOffset"), sizeof(uint32), 1, nullptr, 0);
	FRDGBufferRef OutputOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.OutputOffsetBuffer"), sizeof(uint32), 1, nullptr, 0);

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VisibleInstanceWriteOffsetRDG), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputOffsetBufferRDG), 0);

	// Create buffer for indirect args and upload draw arg data, also clears the instance to zero
	FRDGBufferDesc IndirectArgsDesc = FRDGBufferDesc::CreateIndirectDesc(FInstanceCullingContext::IndirectArgsNumWords * IndirectArgs.Num());
	IndirectArgsDesc.Usage |= BUF_MultiGPUGraphIgnore;

	CullingResult.DrawIndirectArgsRDG = GraphBuilder.CreateBuffer(IndirectArgsDesc, TEXT("Shadow.Virtual.DrawIndirectArgsBuffer"));
	GraphBuilder.QueueBufferUpload(CullingResult.DrawIndirectArgsRDG, IndirectArgs.GetData(), IndirectArgs.GetTypeSize() * IndirectArgs.Num());

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	// Note: we redundantly clear the instance counts here as there is some issue with replays on certain consoles.
	FInstanceCullingContext::AddClearIndirectArgInstanceCountPass(GraphBuilder, ShaderMap, CullingResult.DrawIndirectArgsRDG);

	// not using structured buffer as we have to get at it as a vertex buffer 
	CullingResult.InstanceIdOffsetBufferRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), InstanceIdOffsets.Num()), TEXT("Shadow.Virtual.InstanceIdOffsetBuffer"));

	{
		FCullPerPageDrawCommandsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullPerPageDrawCommandsCs::FParameters>();

		PassParameters->VirtualShadowMap = VirtualShadowMapArray.GetUniformBuffer(0);
		PassParameters->Scene = SceneUniformBuffer.GetBuffer(GraphBuilder);

		PassParameters->OutDirtyPageFlags = GraphBuilder.CreateUAV(VirtualShadowMapArray.DirtyPageFlagsRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);
		PassParameters->DynamicInstanceIdOffset = BatchInfos[0].DynamicInstanceIdOffset;
		PassParameters->DynamicInstanceIdMax = BatchInfos[0].DynamicInstanceIdMax;

		auto GPUData = LoadBalancer->Upload(GraphBuilder);
		GPUData.GetShaderParameters(GraphBuilder, PassParameters->LoadBalancerParameters);

		PassParameters->FirstPrimaryView = VSMCullingBatchInfos[0].FirstPrimaryView;
		PassParameters->NumPrimaryViews = VSMCullingBatchInfos[0].NumPrimaryViews;

		PassParameters->TotalPrimaryViews = TotalPrimaryViews;
		PassParameters->VisibleInstancesBufferNum = CullingResult.MaxNumInstancesPerPass;
		PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
		PassParameters->DrawCommandDescs = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.DrawCommandDescs"), DrawCommandDescs));

		if (bUseBatchMode)
		{
			PassParameters->BatchInfos = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.BatchInfos"), BatchInfos));
			PassParameters->VSMCullingBatchInfos = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VSMCullingBatchInfos"), VSMCullingBatchInfos));
			PassParameters->BatchInds = GraphBuilder.CreateSRV(CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.BatchInds"), BatchInds));
		}

		PassParameters->DrawIndirectArgsBufferOut = GraphBuilder.CreateUAV(CullingResult.DrawIndirectArgsRDG, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);

		PassParameters->VisibleInstancesOut = GraphBuilder.CreateUAV(VisibleInstancesRdg, ERDGUnorderedAccessViewFlags::SkipBarrier);
		PassParameters->VisibleInstanceCountBufferOut = GraphBuilder.CreateUAV(VisibleInstanceWriteOffsetRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);

		PassParameters->NumPageAreaDiagnosticSlots = 0U;

		PassParameters->HZBShaderParameters = HZBShaderParameters;

		FCullPerPageDrawCommandsCs::FPermutationDomain PermutationVector;

		bool bGenerateStats = SetStatsArgsAndPermutation<FCullPerPageDrawCommandsCs>(
			VirtualShadowMapArray.ShouldGenerateStats(), 
			VirtualShadowMapArray.StatsBufferUAV, 
			PassParameters, 
			PermutationVector);

		if (bGenerateStats)
		{
#if !UE_BUILD_SHIPPING
			PassParameters->NumPageAreaDiagnosticSlots = CVarNumPageAreaDiagSlots.GetValueOnRenderThread() < 0 ? FVirtualShadowMapArray::MaxPageAreaDiagnosticSlots : FMath::Min(FVirtualShadowMapArray::MaxPageAreaDiagnosticSlots, uint32(CVarNumPageAreaDiagSlots.GetValueOnRenderThread()));
			PassParameters->LargeInstancePageAreaThreshold = CVarLargeInstancePageAreaThreshold.GetValueOnRenderThread() >= 0 ? CVarLargeInstancePageAreaThreshold.GetValueOnRenderThread() : (VirtualShadowMapArray.GetMaxPhysicalPages() / 8);
#endif
		}

		PermutationVector.Set< FCullPerPageDrawCommandsCs::FBatchedDim >(bUseBatchMode);
		PermutationVector.Set< FCullPerPageDrawCommandsCs::FUseHzbDim >(HZBShaderParameters.HZBTextureArray != nullptr);

		auto ComputeShader = ShaderMap->GetShader<FCullPerPageDrawCommandsCs>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CullPerPageDrawCommands"),
			ComputeShader,
			PassParameters,
			LoadBalancer->GetWrappedCsGroupCount()
		);
	}
	// 2.2.Allocate space for the final instance ID output and so on.
	{
		FAllocateCommandInstanceOutputSpaceCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FAllocateCommandInstanceOutputSpaceCs::FParameters>();

		FRDGBufferRef InstanceIdOutOffsetBufferRDG = CreateStructuredBuffer(GraphBuilder, TEXT("InstanceCulling.OutputOffsetBufferOut"), sizeof(uint32), 1, nullptr, 0);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG), 0);

		PassParameters->NumIndirectArgs = NumIndirectArgs;
		PassParameters->InstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(CullingResult.InstanceIdOffsetBufferRDG, PF_R32_UINT);
		PassParameters->OutputOffsetBufferOut = GraphBuilder.CreateUAV(InstanceIdOutOffsetBufferRDG);
		PassParameters->TmpInstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(TmpInstanceIdOffsetBufferRDG);
		PassParameters->DrawIndirectArgsBuffer = GraphBuilder.CreateSRV(CullingResult.DrawIndirectArgsRDG, PF_R32_UINT);

		auto ComputeShader = ShaderMap->GetShader<FAllocateCommandInstanceOutputSpaceCs>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("AllocateCommandInstanceOutputSpaceCs"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(NumIndirectArgs, FAllocateCommandInstanceOutputSpaceCs::NumThreadsPerGroup)
		);
	}
	// 2.3. Perform final pass to re-shuffle the instance ID's to their final resting places
	CullingResult.InstanceIdsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), CullingResult.MaxNumInstancesPerPass), TEXT("Shadow.Virtual.InstanceIdsBuffer"));
	CullingResult.PageInfoBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), CullingResult.MaxNumInstancesPerPass), TEXT("Shadow.Virtual.PageInfoBuffer"));

	FRDGBufferRef OutputPassIndirectArgs = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(GraphBuilder, FeatureLevel, VisibleInstanceWriteOffsetRDG, TEXT("Shadow.Virtual.IndirectArgs"), FOutputCommandInstanceListsCs::NumThreadsPerGroup);
	{

		FOutputCommandInstanceListsCs::FParameters* PassParameters = GraphBuilder.AllocParameters<FOutputCommandInstanceListsCs::FParameters>();

		PassParameters->VisibleInstances = GraphBuilder.CreateSRV(VisibleInstancesRdg);
		PassParameters->PageInfoBufferOut = GraphBuilder.CreateUAV(CullingResult.PageInfoBuffer);
		PassParameters->InstanceIdsBufferOut = GraphBuilder.CreateUAV(CullingResult.InstanceIdsBuffer);
		PassParameters->TmpInstanceIdOffsetBufferOut = GraphBuilder.CreateUAV(TmpInstanceIdOffsetBufferRDG);
		PassParameters->VisibleInstanceCountBuffer = GraphBuilder.CreateSRV(VisibleInstanceWriteOffsetRDG);
		PassParameters->IndirectArgs = OutputPassIndirectArgs;

		auto ComputeShader = ShaderMap->GetShader<FOutputCommandInstanceListsCs>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("OutputCommandInstanceListsCs"),
			ComputeShader,
			PassParameters,
			OutputPassIndirectArgs,
			0
		);
	}

	return CullingResult;
}

struct FVSMRenderViewCount
{
	uint32 NumPrimaryViews = 0u;
	uint32 NumMipLevels = 0u;
};

FVSMRenderViewCount GetRenderViewCount(const FProjectedShadowInfo* ProjectedShadowInfo)
{
	if (ProjectedShadowInfo->VirtualShadowMapClipmap)
	{
		return { uint32(ProjectedShadowInfo->VirtualShadowMapClipmap->GetLevelCount()), 1u };
	}
	else
	{
		return { ProjectedShadowInfo->bOnePassPointLightShadow ? 6u : 1u, FVirtualShadowMap::MaxMipLevels };
	}
}

static void AddRasterPass(
	FRDGBuilder& GraphBuilder, 
	FRDGEventName&& PassName,
	const FViewInfo* ShadowDepthView, 
	const TRDGUniformBufferRef<FShadowDepthPassUniformParameters>& ShadowDepthPassUniformBuffer,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	FRDGBufferRef VirtualShadowViewsRDG,
	const FCullingResult& CullingResult, 
	FParallelMeshDrawCommandPass& MeshCommandPass,
	FVirtualShadowDepthPassParameters* PassParameters,
	TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> InstanceCullingUniformBuffer,
	TRDGUniformBufferRef<FSceneUniformParameters> SceneUB)
{
	PassParameters->View = ShadowDepthView->ViewUniformBuffer;
	PassParameters->ShadowDepthPass = ShadowDepthPassUniformBuffer;

	PassParameters->VirtualShadowMap = VirtualShadowMapArray.GetUniformBuffer(0);
	PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
	PassParameters->InstanceCullingDrawParams.DrawIndirectArgsBuffer = CullingResult.DrawIndirectArgsRDG;
	PassParameters->InstanceCullingDrawParams.InstanceIdOffsetBuffer = CullingResult.InstanceIdOffsetBufferRDG;
	PassParameters->InstanceCullingDrawParams.InstanceCulling = InstanceCullingUniformBuffer;
	PassParameters->InstanceCullingDrawParams.Scene = SceneUB;

	FIntRect ViewRect;
	ViewRect.Max = FVirtualShadowMap::VirtualMaxResolutionXY;

	GraphBuilder.AddPass(
		MoveTemp(PassName),
		PassParameters,
		ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
		[&MeshCommandPass, PassParameters, ViewRect](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			FRHIRenderPassInfo RPInfo;
			RPInfo.ResolveRect = FResolveRect(ViewRect);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("RasterizeVirtualShadowMaps(Non-Nanite)"));

			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min(ViewRect.Max.X, 32767), FMath::Min(ViewRect.Max.Y, 32767), 1.0f);

			MeshCommandPass.Draw(RHICmdList, &PassParameters->InstanceCullingDrawParams);
			RHICmdList.EndRenderPass();
		});
}

// TODO: replace translation from old system to direct fetching from new, move to shadow scene renderer
static FCullingVolume GetCullingVolume(const FProjectedShadowInfo* ProjectedShadowInfo)
{
	FCullingVolume CullingVolume;

	CullingVolume.WorldToVolumeTranslation = ProjectedShadowInfo->PreShadowTranslation;

	uint32 NumPrimaryViews = 0;
	if (ProjectedShadowInfo->VirtualShadowMapClipmap)
	{
		const bool bIsCached = ProjectedShadowInfo->VirtualShadowMapClipmap->GetCacheEntry() && !ProjectedShadowInfo->VirtualShadowMapClipmap->GetCacheEntry()->IsUncached();

		// We can only do this culling if the light is both uncached & it is using the accurate bounds (i.e., r.Shadow.Virtual.Clipmap.UseConservativeCulling is turned off).
		if (!bIsCached && !ProjectedShadowInfo->CascadeSettings.ShadowBoundsAccurate.Planes.IsEmpty())
		{
			CullingVolume.ConvexVolume = ProjectedShadowInfo->CascadeSettings.ShadowBoundsAccurate;
			// ShadowBoundsAccurate is in world-space
			CullingVolume.WorldToVolumeTranslation = FVector3d::ZeroVector;
		}
		else
		{
			CullingVolume.Sphere = ProjectedShadowInfo->VirtualShadowMapClipmap->GetBoundingSphere();
			CullingVolume.ConvexVolume = ProjectedShadowInfo->VirtualShadowMapClipmap->GetViewFrustumBounds();
		}
	}
	else
	{
		CullingVolume.Sphere = ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetBoundingSphere();
		CullingVolume.ConvexVolume = ProjectedShadowInfo->CasterOuterFrustum;
	}
	return CullingVolume;
}

Nanite::FPackedViewArray* FVirtualShadowMapArray::CreateVirtualShadowMapNaniteViews(FRDGBuilder& GraphBuilder, TConstArrayView<FViewInfo> Views, TConstArrayView<FProjectedShadowInfo*> Shadows, float ShadowsLODScaleFactor, FSceneInstanceCullingQuery* InstanceCullingQuery)
{
	uint32 TotalPrimaryViews = 0;
	uint32 MaxNumMips = 0;

	// TODO: replace with persistent and incrementally updated setup
	for (FProjectedShadowInfo* ProjectedShadowInfo : Shadows)
	{
		if (ProjectedShadowInfo->bShouldRenderVSM)
		{
			FVSMRenderViewCount VSMRenderViewCount = GetRenderViewCount(ProjectedShadowInfo);
			MaxNumMips = FMath::Max(MaxNumMips, VSMRenderViewCount.NumMipLevels);

			if (InstanceCullingQuery)
			{
				// Add a shadow thingo to be culled, need to know the primary view ranges.
				// TODO: Move with other connecting-the-dots stuff into shadow scene renderer.
				InstanceCullingQuery->Add(TotalPrimaryViews, VSMRenderViewCount.NumPrimaryViews, VSMRenderViewCount.NumMipLevels * VSMRenderViewCount.NumPrimaryViews, GetCullingVolume(ProjectedShadowInfo));
			}
			TotalPrimaryViews += VSMRenderViewCount.NumPrimaryViews;
		}
	}

	if (TotalPrimaryViews == 0)
	{
		return nullptr;
	}

	return Nanite::FPackedViewArray::CreateWithSetupTask(
		GraphBuilder,
		TotalPrimaryViews,
		MaxNumMips,
		[this, Views, Shadows, ShadowsLODScaleFactor] (Nanite::FPackedViewArray::ArrayType& VirtualShadowViews)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddNaniteRenderViews);

		for (FProjectedShadowInfo* Shadow : Shadows)
		{
			if (Shadow->bShouldRenderVSM)
			{
				AddRenderViews(
					Shadow,
					Views,
					ShadowsLODScaleFactor,
					bUseHzbOcclusion,
					bUseHzbOcclusion,
					Shadow->ShouldClampToNearPlane(),
					VirtualShadowViews);
			}
		}

		CreateMipViews(VirtualShadowViews);
	});
}

void FVirtualShadowMapArray::RenderVirtualShadowMapsNanite(FRDGBuilder& GraphBuilder, FSceneRenderer& SceneRenderer, bool bUpdateNaniteStreaming, const FNaniteVisibilityQuery* VisibilityQuery, Nanite::FPackedViewArray* VirtualShadowMapViews, FSceneInstanceCullingQuery* SceneInstanceCullingQuery)
{
	bool bCsvLogEnabled = IsCsvLogEnabled();

	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualShadowMapArray::RenderVirtualShadowMapsNanite);
	RDG_EVENT_SCOPE(GraphBuilder, "RenderVirtualShadowMaps(Nanite)");

	const FIntPoint VirtualShadowSize = GetPhysicalPoolSize();
	const FIntRect VirtualShadowViewRect = FIntRect(0, 0, VirtualShadowSize.X, VirtualShadowSize.Y);

	Nanite::FSharedContext SharedContext{};
	SharedContext.FeatureLevel = SceneRenderer.FeatureLevel;
	SharedContext.ShaderMap = GetGlobalShaderMap(SharedContext.FeatureLevel);
	SharedContext.Pipeline = Nanite::EPipeline::Shadows;

	check(PhysicalPagePoolRDG != nullptr);

	Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(
		GraphBuilder,
		SharedContext,
		SceneRenderer.ViewFamily,
		VirtualShadowSize,
		VirtualShadowViewRect,
		Nanite::EOutputBufferMode::DepthOnly,
		false,	// Clear entire texture
		true, // bAsyncCompute
		nullptr, 0,
		PhysicalPagePoolRDG,
		false, // Custom pass
		bEnableNaniteVisualization,
		bEnableNaniteVisualization	// Overdraw is the only currently supported mode
	);

	const FViewInfo& SceneView = SceneRenderer.Views[0];

	static FString VirtualFilterName = TEXT("VirtualShadowMaps");

	if (VirtualShadowMapViews)
	{
		SET_DWORD_STAT(STAT_VSMNaniteViewsPrimary, VirtualShadowMapViews->NumPrimaryViews);

		// Prev HZB requires previous page tables and similar
		bool bPrevHZBValid = HZBPhysicalArray != nullptr && CacheManager->GetPrevBuffers().PageTable != nullptr;

		Nanite::FConfiguration CullingConfig = { 0 };
		CullingConfig.bIsShadowPass = true;
		CullingConfig.bUpdateStreaming = bUpdateNaniteStreaming;
		CullingConfig.bTwoPassOcclusion = UseTwoPassHzbOcclusion();
		CullingConfig.bExtractStats = Nanite::IsStatFilterActive(VirtualFilterName);
		CullingConfig.SetViewFlags(SceneView);

		auto NaniteRenderer = Nanite::IRenderer::Create(
			GraphBuilder,
			Scene,
			SceneView,
			SceneRenderer.GetSceneUniforms(),
			SharedContext,
			RasterContext,
			CullingConfig,
			VirtualShadowViewRect,
			bPrevHZBValid ? HZBPhysicalArray : nullptr,
			this
		);

		if (bCsvLogEnabled)
		{
			//CullingContext.RenderFlags |= NANITE_RENDER_FLAG_WRITE_STATS;	FIXME
		}

		NaniteRenderer->DrawGeometry(
			Scene.NaniteRasterPipelines[ENaniteMeshPass::BasePass],
			VisibilityQuery,
			*VirtualShadowMapViews,
			SceneInstanceCullingQuery
		);

		if (bCsvLogEnabled)
		{
			//StatsNaniteBufferRDG = CullingContext.StatsBuffer;		FIXME
		}
	}

	if (bUseHzbOcclusion)
	{
		UpdateHZB(GraphBuilder);
	}
}

void FVirtualShadowMapArray::RenderVirtualShadowMapsNonNanite(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& SceneUniformBuffer, const TArray<FProjectedShadowInfo *, SceneRenderingAllocator>& VirtualSmMeshCommandPasses, TArrayView<FViewInfo> Views)
{
	if (VirtualSmMeshCommandPasses.Num() == 0)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualShadowMapArray::RenderVirtualShadowMapsNonNanite);
	RDG_EVENT_SCOPE(GraphBuilder, "RenderVirtualShadowMaps(Non-Nanite)");

	FGPUScene& GPUScene = Scene.GPUScene;
		
	int32 HZBMode = CVarNonNaniteVsmUseHzb.GetValueOnRenderThread();
	// When disabling Nanite, there may be stale data in the Nanite-HZB causing incorrect culling.
	if (!bHZBBuiltThisFrame)
	{
		HZBMode = 0; /* Disable HZB culling */
	}
	FRDGTextureRef HZBTextureArray = (HZBMode > 0 && CacheManager->IsHZBDataAvailable()) ? HZBPhysicalArrayRDG : nullptr;

	TArray<FVSMCullingBatchInfo, SceneRenderingAllocator> UnBatchedVSMCullingBatchInfo;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> BatchedVirtualSmMeshCommandPasses;
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> UnBatchedVirtualSmMeshCommandPasses;
	UnBatchedVSMCullingBatchInfo.Reserve(VirtualSmMeshCommandPasses.Num());
	BatchedVirtualSmMeshCommandPasses.Reserve(VirtualSmMeshCommandPasses.Num());
	UnBatchedVirtualSmMeshCommandPasses.Reserve(VirtualSmMeshCommandPasses.Num());

	TArray<FProjectedShadowInfo*, SceneRenderingAllocator> ShadowsToAddRenderViews;

	TArray<FVSMCullingBatchInfo, SceneRenderingAllocator> VSMCullingBatchInfos;
	VSMCullingBatchInfos.Reserve(VirtualSmMeshCommandPasses.Num());

	TArray<FVirtualShadowDepthPassParameters*, SceneRenderingAllocator> BatchedPassParameters;
	BatchedPassParameters.Reserve(VirtualSmMeshCommandPasses.Num());

	uint32 MaxNumMips = 0;
	uint32 TotalPrimaryViews = 0;
	uint32 TotalViews = 0;

	FInstanceCullingMergedContext InstanceCullingMergedContext(GPUScene.GetShaderPlatform(), true);
	// We don't use the registered culling views (this redundancy should probably be addressed at some point), set the number to disable index range checking
	InstanceCullingMergedContext.NumCullingViews = -1;
	int32 TotalPreCullInstanceCount = 0;
	// Instance count multiplied by the number of (VSM) views, gives a safe maximum number of possible output instances from culling.
	uint32 TotalViewScaledInstanceCount = 0u;
	for (int32 Index = 0; Index < VirtualSmMeshCommandPasses.Num(); ++Index)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = VirtualSmMeshCommandPasses[Index];

		if (!ProjectedShadowInfo->bShouldRenderVSM)
		{
			continue;
		}

		ProjectedShadowInfo->BeginRenderView(GraphBuilder, &Scene);

		FVSMCullingBatchInfo VSMCullingBatchInfo;
		VSMCullingBatchInfo.FirstPrimaryView = TotalPrimaryViews;
		VSMCullingBatchInfo.NumPrimaryViews = 0U;

		const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap = ProjectedShadowInfo->VirtualShadowMapClipmap;
		check(Clipmap.IsValid() || ProjectedShadowInfo->HasVirtualShadowMap());
		{
			FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
			FInstanceCullingContext* InstanceCullingContext = MeshCommandPass.GetInstanceCullingContext();
			InstanceCullingContext->WaitForSetupTask();

			TotalPreCullInstanceCount += InstanceCullingContext->TotalInstances;

			if (InstanceCullingContext->HasCullingCommands())
			{
				FVSMRenderViewCount VSMRenderViewCount = GetRenderViewCount(ProjectedShadowInfo);
				MaxNumMips = FMath::Max(MaxNumMips, VSMRenderViewCount.NumMipLevels);

				TotalViewScaledInstanceCount += InstanceCullingContext->TotalInstances * VSMRenderViewCount.NumPrimaryViews * VSMRenderViewCount.NumMipLevels;

				VSMCullingBatchInfo.NumPrimaryViews = VSMRenderViewCount.NumPrimaryViews;
				TotalPrimaryViews += VSMRenderViewCount.NumPrimaryViews;
				ShadowsToAddRenderViews.Add(ProjectedShadowInfo);

				if (CVarDoNonNaniteBatching.GetValueOnRenderThread() != 0)
				{
					// NOTE: This array must be 1:1 with the batches inside the InstanceCullingMergedContext, which is guaranteed by checking HasCullingCommands() above (and checked in the merged context)
					//       If we were to defer/async this process, we need to maintain this property or add some remapping.
					VSMCullingBatchInfos.Add(VSMCullingBatchInfo);

					// Note: we have to allocate these up front as the context merging machinery writes the offsets directly to the &PassParameters->InstanceCullingDrawParams, 
					// this is a side-effect from sharing the code with the deferred culling. Should probably be refactored.
					FVirtualShadowDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualShadowDepthPassParameters>();
					InstanceCullingMergedContext.AddBatch(GraphBuilder, InstanceCullingContext, &PassParameters->InstanceCullingDrawParams);
					BatchedVirtualSmMeshCommandPasses.Add(ProjectedShadowInfo);
					BatchedPassParameters.Add(PassParameters);
				}
				else
				{
					UnBatchedVSMCullingBatchInfo.Add(VSMCullingBatchInfo);
					UnBatchedVirtualSmMeshCommandPasses.Add(ProjectedShadowInfo);
				}
			}
		}
	}

	FRDGBuffer* VirtualShadowViewsRDG = nullptr;

	if (!ShadowsToAddRenderViews.IsEmpty())
	{
		Nanite::FPackedViewArray* ViewArray = Nanite::FPackedViewArray::CreateWithSetupTask(
			GraphBuilder,
			TotalPrimaryViews,
			MaxNumMips,
			[this, Views, ShadowsToAddRenderViews = MoveTemp(ShadowsToAddRenderViews), bHasHZBTexture = (HZBTextureArray != nullptr)] (Nanite::FPackedViewArray::ArrayType& OutShadowViews)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AddNonNaniteRenderViews);
			for (FProjectedShadowInfo* ProjectedShadowInfo : ShadowsToAddRenderViews)
			{
				AddRenderViews(ProjectedShadowInfo, Views, 1.0f, bHasHZBTexture, false, ProjectedShadowInfo->ShouldClampToNearPlane(), OutShadowViews);
			}
			CreateMipViews(OutShadowViews);
		});

		VirtualShadowViewsRDG = CreateStructuredBuffer(GraphBuilder, TEXT("Shadow.Virtual.VirtualShadowViews"), [ViewArray]() -> const typename Nanite::FPackedViewArray::ArrayType& { return ViewArray->GetViews(); });
	}

	CSV_CUSTOM_STAT(VSM, NonNanitePreCullInstanceCount, TotalPreCullInstanceCount, ECsvCustomStatOp::Set);

	// Helper function to create raster pass UB - only really need two of these ever
	const FSceneTextures* SceneTextures = &GetViewFamilyInfo(Views).GetSceneTextures();
	auto CreateShadowDepthPassUniformBuffer = [this, &VirtualShadowViewsRDG, &GraphBuilder, SceneTextures](bool bClampToNearPlane)
	{
		FShadowDepthPassUniformParameters* ShadowDepthPassParameters = GraphBuilder.AllocParameters<FShadowDepthPassUniformParameters>();
		check(PhysicalPagePoolRDG != nullptr);
		// TODO: These are not used for this case anyway
		ShadowDepthPassParameters->ProjectionMatrix = FMatrix44f::Identity;
		ShadowDepthPassParameters->ViewMatrix = FMatrix44f::Identity;
		ShadowDepthPassParameters->ShadowParams = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
		ShadowDepthPassParameters->bRenderToVirtualShadowMap = true;

		ShadowDepthPassParameters->VirtualSmPageTable = GraphBuilder.CreateSRV(PageTableRDG);
		ShadowDepthPassParameters->PackedNaniteViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
		ShadowDepthPassParameters->AllocatedPageRectBounds = GraphBuilder.CreateSRV(AllocatedPageRectBoundsRDG);
		ShadowDepthPassParameters->UncachedPageRectBounds = GraphBuilder.CreateSRV(UncachedPageRectBoundsRDG);
		ShadowDepthPassParameters->OutDepthBufferArray = GraphBuilder.CreateUAV(PhysicalPagePoolRDG, ERDGUnorderedAccessViewFlags::SkipBarrier);
		SetupSceneTextureUniformParameters(GraphBuilder, SceneTextures, Scene.GetFeatureLevel(), ESceneTextureSetupMode::None, ShadowDepthPassParameters->SceneTextures);
		ShadowDepthPassParameters->bClampToNearPlane = bClampToNearPlane;

		return GraphBuilder.CreateUniformBuffer(ShadowDepthPassParameters);
	};

	FCullPerPageDrawCommandsCs::FHZBShaderParameters HZBShaderParameters;
	if (HZBTextureArray)
	{
		if (HZBMode == 2)
		{
			// Mode 2 uses the current frame HZB & page table.
			HZBShaderParameters.HZBPageTable = GraphBuilder.CreateSRV(PageTableRDG);
			HZBShaderParameters.HZBPageFlags = GraphBuilder.CreateSRV(PageFlagsRDG);
			HZBShaderParameters.HZBPageRectBounds = GraphBuilder.CreateSRV(AllocatedPageRectBoundsRDG);	// TODO: Uncached?
		}
		else
		{
			const FVirtualShadowMapArrayFrameData& PrevBuffers = CacheManager->GetPrevBuffers();
			HZBShaderParameters.HZBPageTable = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(PrevBuffers.PageTable, TEXT("Shadow.Virtual.PrevPageTable")));
			HZBShaderParameters.HZBPageFlags = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(PrevBuffers.PageFlags, TEXT("Shadow.Virtual.PrevPageFlags")));
			HZBShaderParameters.HZBPageRectBounds = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(PrevBuffers.AllocatedPageRectBounds, TEXT("Shadow.Virtual.PrevAllocatedPageRectBounds")));	// TODO: Uncached?
		}
		check(HZBShaderParameters.HZBPageTable);
		check(HZBShaderParameters.HZBPageFlags);
		check(HZBShaderParameters.HZBPageRectBounds);
				
		HZBShaderParameters.HZBTextureArray = HZBTextureArray;
		HZBShaderParameters.HZBSize = HZBTextureArray->Desc.Extent;
		HZBShaderParameters.HZBSampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();
		HZBShaderParameters.HZBMode = HZBMode;
	}
	else
	{
		HZBShaderParameters.HZBTextureArray = nullptr;
	}

	TRDGUniformBufferRef<FSceneUniformParameters> SceneUB = SceneUniformBuffer.GetBuffer(GraphBuilder);

	// Process batched passes
	if (!InstanceCullingMergedContext.Batches.IsEmpty())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Batched");

		InstanceCullingMergedContext.MergeBatches();

		FCullingResult CullingResult;
		{
		    RDG_EVENT_SCOPE(GraphBuilder, "CullingPasses");
		    CullingResult = AddCullingPasses(
			    GraphBuilder,
			    InstanceCullingMergedContext.IndirectArgs,
			    InstanceCullingMergedContext.DrawCommandDescs,
			    InstanceCullingMergedContext.InstanceIdOffsets,
			    &InstanceCullingMergedContext.LoadBalancers[FInstanceCullingMergedContext::FirstGenericBinIndex],
			    InstanceCullingMergedContext.BatchInfos,
			    VSMCullingBatchInfos,
			    InstanceCullingMergedContext.BatchInds[FInstanceCullingMergedContext::FirstGenericBinIndex],
			    InstanceCullingMergedContext.TotalInstances,
			    TotalViewScaledInstanceCount,
			    TotalPrimaryViews,
			    VirtualShadowViewsRDG,
			    HZBShaderParameters,
			    *this,
			    SceneUniformBuffer,
			    GPUScene.GetFeatureLevel()
		    );
		}

		TRDGUniformBufferRef<FShadowDepthPassUniformParameters> ShadowDepthPassUniformBuffer = CreateShadowDepthPassUniformBuffer(false);

		FInstanceCullingGlobalUniforms* InstanceCullingGlobalUniforms = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
		InstanceCullingGlobalUniforms->InstanceIdsBuffer = GraphBuilder.CreateSRV(CullingResult.InstanceIdsBuffer);
		InstanceCullingGlobalUniforms->PageInfoBuffer = GraphBuilder.CreateSRV(CullingResult.PageInfoBuffer);
		InstanceCullingGlobalUniforms->BufferCapacity = CullingResult.MaxNumInstancesPerPass;
		TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> InstanceCullingUniformBuffer = GraphBuilder.CreateUniformBuffer(InstanceCullingGlobalUniforms);

		if(!BatchedVirtualSmMeshCommandPasses.IsEmpty())
		{
			if (CVarVirtualShadowSinglePassBatched.GetValueOnRenderThread() != 0)
			{
				FVirtualShadowDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualShadowDepthPassParameters>();
				FProjectedShadowInfo* ProjectedShadowInfo0 = BatchedVirtualSmMeshCommandPasses[0];
				FViewInfo* ShadowDepthView = ProjectedShadowInfo0->ShadowDepthView;

				PassParameters->View = ShadowDepthView->ViewUniformBuffer;
				PassParameters->ShadowDepthPass = ShadowDepthPassUniformBuffer;

				PassParameters->VirtualShadowMap = GetUniformBuffer(0);
				PassParameters->InViews = GraphBuilder.CreateSRV(VirtualShadowViewsRDG);
				PassParameters->InstanceCullingDrawParams.DrawIndirectArgsBuffer = CullingResult.DrawIndirectArgsRDG;
				PassParameters->InstanceCullingDrawParams.InstanceIdOffsetBuffer = CullingResult.InstanceIdOffsetBufferRDG;
				PassParameters->InstanceCullingDrawParams.InstanceCulling = InstanceCullingUniformBuffer;
				PassParameters->InstanceCullingDrawParams.Scene = SceneUB;
				PassParameters->InstanceCullingDrawParams.IndirectArgsByteOffset = 0U;
				PassParameters->InstanceCullingDrawParams.InstanceDataByteOffset = 0U;


				GraphBuilder.AddPass(
					RDG_EVENT_NAME("RasterPasses"),
					PassParameters,
					ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
					[PassParameters, 
					BatchedPassParameters=MoveTemp(BatchedPassParameters), 
					BatchedVirtualSmMeshCommandPasses=MoveTemp(BatchedVirtualSmMeshCommandPasses)](FRDGAsyncTask, FRHICommandList& RHICmdList)
			{
						FIntRect ViewRect;
						ViewRect.Min = FIntPoint(0, 0);
						ViewRect.Max = FVirtualShadowMap::VirtualMaxResolutionXY;
						FRHIRenderPassInfo RPInfo;
						RPInfo.ResolveRect = FResolveRect(ViewRect);
						RHICmdList.BeginRenderPass(RPInfo, TEXT("RasterizeVirtualShadowMaps(Non-Nanite)"));

						RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, FMath::Min(ViewRect.Max.X, 32767), FMath::Min(ViewRect.Max.Y, 32767), 1.0f);

						for (int32 Index = 0; Index < BatchedVirtualSmMeshCommandPasses.Num(); ++Index)
						{
							FProjectedShadowInfo* ProjectedShadowInfo = BatchedVirtualSmMeshCommandPasses[Index];
							FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();

							FInstanceCullingDrawParams InstanceCullingDrawParams = PassParameters->InstanceCullingDrawParams;
							InstanceCullingDrawParams.IndirectArgsByteOffset = BatchedPassParameters[Index]->InstanceCullingDrawParams.IndirectArgsByteOffset;
							InstanceCullingDrawParams.InstanceDataByteOffset = BatchedPassParameters[Index]->InstanceCullingDrawParams.InstanceDataByteOffset;
#if WITH_PROFILEGPU
							FString LightNameWithLevel;
							if (GVSMShowLightDrawEvents != 0)
							{
								FSceneRenderer::GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
							}
							SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, BatchedNonNanite, GVSMShowLightDrawEvents != 0, TEXT("%s"), LightNameWithLevel);
#endif
							MeshCommandPass.Draw(RHICmdList, &InstanceCullingDrawParams);
						}

						RHICmdList.EndRenderPass();
					});

			}
			else
			{
				RDG_EVENT_SCOPE(GraphBuilder, "RasterPasses");
				for (int32 Index = 0; Index < BatchedVirtualSmMeshCommandPasses.Num(); ++Index)
				{
					FProjectedShadowInfo* ProjectedShadowInfo = BatchedVirtualSmMeshCommandPasses[Index];
					FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
					FViewInfo* ShadowDepthView = ProjectedShadowInfo->ShadowDepthView;

					FString LightNameWithLevel;
					FSceneRenderer::GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
					AddRasterPass(GraphBuilder, RDG_EVENT_NAME("Rasterize[%s]", *LightNameWithLevel), ShadowDepthView, ShadowDepthPassUniformBuffer, *this, VirtualShadowViewsRDG, CullingResult, MeshCommandPass, BatchedPassParameters[Index], InstanceCullingUniformBuffer, SceneUB);
			}
		}
	}
	}

	// Loop over the un batched mesh command passes needed, these are all the clipmaps (but we may change the criteria)
	for (int32 Index = 0; Index < UnBatchedVirtualSmMeshCommandPasses.Num(); ++Index)
	{
		const auto VSMCullingBatchInfo = UnBatchedVSMCullingBatchInfo[Index];
		FProjectedShadowInfo* ProjectedShadowInfo = UnBatchedVirtualSmMeshCommandPasses[Index];
		FInstanceCullingMergedContext::FContextBatchInfoPacked CullingBatchInfo = FInstanceCullingMergedContext::FContextBatchInfoPacked{ 0 };

		FParallelMeshDrawCommandPass& MeshCommandPass = ProjectedShadowInfo->GetShadowDepthPass();
		const TSharedPtr<FVirtualShadowMapClipmap> Clipmap = ProjectedShadowInfo->VirtualShadowMapClipmap;
		FViewInfo* ShadowDepthView = ProjectedShadowInfo->ShadowDepthView;

		MeshCommandPass.WaitForSetupTask();

		FInstanceCullingContext* InstanceCullingContext = MeshCommandPass.GetInstanceCullingContext();

		if (InstanceCullingContext->HasCullingCommands())
		{
			FString LightNameWithLevel;
			FSceneRenderer::GetLightNameForDrawEvent(ProjectedShadowInfo->GetLightSceneInfo().Proxy, LightNameWithLevel);
			RDG_EVENT_SCOPE(GraphBuilder, "%s", *LightNameWithLevel);

			FVSMRenderViewCount VSMRenderViewCount = GetRenderViewCount(ProjectedShadowInfo);
			uint32 ViewScaledInstanceCount = VSMRenderViewCount.NumPrimaryViews * VSMRenderViewCount.NumMipLevels * InstanceCullingContext->TotalInstances;

			CullingBatchInfo.DynamicInstanceIdOffset = ShadowDepthView->DynamicPrimitiveCollector.GetInstanceSceneDataOffset();
			CullingBatchInfo.DynamicInstanceIdMax = CullingBatchInfo.DynamicInstanceIdOffset + ShadowDepthView->DynamicPrimitiveCollector.NumInstances();

			FCullingResult CullingResult = AddCullingPasses(
				GraphBuilder,
				InstanceCullingContext->IndirectArgs, 
				InstanceCullingContext->DrawCommandDescs,
				InstanceCullingContext->InstanceIdOffsets,
				InstanceCullingContext->LoadBalancers[FInstanceCullingMergedContext::FirstGenericBinIndex],
				MakeArrayView(&CullingBatchInfo, 1),
				MakeArrayView(&VSMCullingBatchInfo, 1),
				MakeArrayView<const uint32>(nullptr, 0),
				InstanceCullingContext->TotalInstances,
				ViewScaledInstanceCount,
				TotalPrimaryViews,
				VirtualShadowViewsRDG,
				HZBShaderParameters,
				*this,
				SceneUniformBuffer,
				GPUScene.GetFeatureLevel()
			);

			TRDGUniformBufferRef<FShadowDepthPassUniformParameters> ShadowDepthPassUniformBuffer = CreateShadowDepthPassUniformBuffer(ProjectedShadowInfo->ShouldClampToNearPlane());

			FInstanceCullingGlobalUniforms* InstanceCullingGlobalUniforms = GraphBuilder.AllocParameters<FInstanceCullingGlobalUniforms>();
			InstanceCullingGlobalUniforms->InstanceIdsBuffer = GraphBuilder.CreateSRV(CullingResult.InstanceIdsBuffer);
			InstanceCullingGlobalUniforms->PageInfoBuffer = GraphBuilder.CreateSRV(CullingResult.PageInfoBuffer);
			InstanceCullingGlobalUniforms->BufferCapacity = CullingResult.MaxNumInstancesPerPass;
			TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> InstanceCullingUniformBuffer = GraphBuilder.CreateUniformBuffer(InstanceCullingGlobalUniforms);

			FVirtualShadowDepthPassParameters* DepthPassParams = GraphBuilder.AllocParameters<FVirtualShadowDepthPassParameters>();
			DepthPassParams->InstanceCullingDrawParams.IndirectArgsByteOffset = 0;
			DepthPassParams->InstanceCullingDrawParams.InstanceDataByteOffset = 0;
			AddRasterPass(GraphBuilder, RDG_EVENT_NAME("Rasterize"), ShadowDepthView, ShadowDepthPassUniformBuffer, *this, VirtualShadowViewsRDG, CullingResult, MeshCommandPass, DepthPassParams, InstanceCullingUniformBuffer, SceneUB);
		}


		//
		if (Index == CVarShowClipmapStats.GetValueOnRenderThread())
		{
			// The 'main' view the shadow was created with respect to
			const FViewInfo* ViewUsedToCreateShadow = ProjectedShadowInfo->DependentView;
			const FViewInfo& View = *ViewUsedToCreateShadow;

			FVirtualSmPrintClipmapStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmPrintClipmapStatsCS::FParameters>();

			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintStruct);
			//PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
			PassParameters->ShadowMapIdRangeStart = Clipmap->GetVirtualShadowMapId();
			// Note: assumes range!
			PassParameters->ShadowMapIdRangeEnd = Clipmap->GetVirtualShadowMapId() + Clipmap->GetLevelCount();
			PassParameters->AllocatedPageRectBounds = GraphBuilder.CreateSRV(AllocatedPageRectBoundsRDG);

			auto ComputeShader = View.ShaderMap->GetShader<FVirtualSmPrintClipmapStatsCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("PrintClipmapStats"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1)
			);
		}
	}

	// Update the dirty page flags & the page table meta data for invalidations.
	// TODO: This pass probably needs to run even if there's no non-nanite geometry? Double check the early outs above
	{
		FUpdateAndClearDirtyFlagsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FUpdateAndClearDirtyFlagsCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
		PassParameters->DirtyPageFlagsInOut = GraphBuilder.CreateUAV(DirtyPageFlagsRDG);
		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FUpdateAndClearDirtyFlagsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("UpdateAndClearDirtyFlags"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(UniformParameters.MaxPhysicalPages, FUpdateAndClearDirtyFlagsCS::DefaultCSGroupX), 1, 1)
		);
	}
}

class FSelectPagesForHZBAndUpdateDirtyFlagsCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FSelectPagesForHZBAndUpdateDirtyFlagsCS);
	SHADER_USE_PARAMETER_STRUCT(FSelectPagesForHZBAndUpdateDirtyFlagsCS, FVirtualShadowMapPageManagementShader)

	class FGenerateStatsDim : SHADER_PERMUTATION_BOOL("VSM_GENERATE_STATS");
	using FPermutationDomain = TShaderPermutationDomain< FGenerateStatsDim >;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPhysicalPageMetaData>, OutPhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutPagesForHZBIndirectArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutPhysicalPagesForHZB)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, DirtyPageFlagsInOut)
		SHADER_PARAMETER(uint32, bFirstBuildThisFrame)
		SHADER_PARAMETER(uint32, bForceFullHZBUpdate)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer< uint >, OutStatsBuffer)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectPagesForHZBAndUpdateDirtyFlagsCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "SelectPagesForHZBAndUpdateDirtyFlagsCS", SF_Compute);

class FVirtualSmBuildHZBPerPageCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmBuildHZBPerPageCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmBuildHZBPerPageCS, FVirtualShadowMapPageManagementShader)

	static constexpr uint32 TotalHZBLevels = FVirtualShadowMap::NumHZBLevels;
	static constexpr uint32 HZBLevelsBase = TotalHZBLevels - 2U;

	static_assert(HZBLevelsBase == 5U, "The shader is expecting 5 levels, if the page size is changed, this needs to be massaged");

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhysicalPageMetaData>, PhysicalPageMetaData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesForHZB)
		SHADER_PARAMETER_SAMPLER(SamplerState, PhysicalPagePoolSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<uint>, PhysicalPagePool)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2DArray<float>, FurthestHZBArrayOutput, [HZBLevelsBase])
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmBuildHZBPerPageCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "BuildHZBPerPageCS", SF_Compute);


class FVirtualSmBBuildHZBPerPageTopCS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FVirtualSmBBuildHZBPerPageTopCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualSmBBuildHZBPerPageTopCS, FVirtualShadowMapPageManagementShader)

	// We need one level less as HZB starts at half-size (not really sure if we really need 1x1 and 2x2 sized levels).
	static constexpr uint32 HZBLevelsTop = 2;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualShadowMapUniformParameters, VirtualShadowMap)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer< uint >, PhysicalPagesForHZB)
		SHADER_PARAMETER_SAMPLER(SamplerState, ParentTextureMipSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray, ParentTextureArrayMip)
		SHADER_PARAMETER(FVector2f, InvHzbInputSize)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D<float>, FurthestHZBArrayOutput, [HZBLevelsTop])
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FVirtualSmBBuildHZBPerPageTopCS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf", "BuildHZBPerPageTopCS", SF_Compute);

void FVirtualShadowMapArray::UpdateHZB(FRDGBuilder& GraphBuilder)
{
	const FIntRect ViewRect(0, 0, GetPhysicalPoolSize().X, GetPhysicalPoolSize().Y);

	// 1. Gather up all physical pages that are allocated
	FRDGBufferRef PagesForHZBIndirectArgsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(2U * 4U), TEXT("Shadow.Virtual.PagesForHZBIndirectArgs"));
	// NOTE: Total allocated pages since the shader outputs separate entries for static/dynamic pages
	FRDGBufferRef PhysicalPagesForHZBRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), GetTotalAllocatedPhysicalPages() + 1), TEXT("Shadow.Virtual.PhysicalPagesForHZB"));

	// 1. Clear the indirect args buffer (note 2x args)
	AddClearIndirectDispatchArgs1DPass(GraphBuilder, Scene.GetFeatureLevel(), PagesForHZBIndirectArgsRDG, 2u, 4u);

	// 2. Filter the relevant physical pages and set up the indirect args
	{
		FSelectPagesForHZBAndUpdateDirtyFlagsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSelectPagesForHZBAndUpdateDirtyFlagsCS::FParameters>();
		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		PassParameters->OutPagesForHZBIndirectArgsBuffer = GraphBuilder.CreateUAV(PagesForHZBIndirectArgsRDG);
		PassParameters->OutPhysicalPagesForHZB = GraphBuilder.CreateUAV(PhysicalPagesForHZBRDG);
		PassParameters->DirtyPageFlagsInOut = GraphBuilder.CreateUAV(DirtyPageFlagsRDG);
		PassParameters->OutPhysicalPageMetaData = GraphBuilder.CreateUAV(PhysicalPageMetaDataRDG);
		PassParameters->bFirstBuildThisFrame = !bHZBBuiltThisFrame;
		PassParameters->bForceFullHZBUpdate = CVarShadowsVirtualForceFullHZBUpdate.GetValueOnRenderThread();
		FSelectPagesForHZBAndUpdateDirtyFlagsCS::FPermutationDomain PermutationVector;
		SetStatsArgsAndPermutation<FSelectPagesForHZBAndUpdateDirtyFlagsCS>(ShouldGenerateStats(), StatsBufferUAV, PassParameters, PermutationVector);
		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FSelectPagesForHZBAndUpdateDirtyFlagsCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SelectPagesForHZB"),
			ComputeShader,
			PassParameters,
			FIntVector(FMath::DivideAndRoundUp(UniformParameters.MaxPhysicalPages, FSelectPagesForHZBAndUpdateDirtyFlagsCS::DefaultCSGroupX), 1, 1)
		);
	}

	bHZBBuiltThisFrame = true;
	const int HZBPoolArraySize = HasSeparateDynamicHZB() ? 2 : 1;
		
	{
		FVirtualSmBuildHZBPerPageCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmBuildHZBPerPageCS::FParameters>();

		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);
		for (int32 DestMip = 0; DestMip < FVirtualSmBuildHZBPerPageCS::HZBLevelsBase; DestMip++)
		{
			PassParameters->FurthestHZBArrayOutput[DestMip] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(HZBPhysicalArrayRDG, DestMip));
		}
		PassParameters->PhysicalPagePool = PhysicalPagePoolRDG;
		PassParameters->PhysicalPagePoolSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->PhysicalPageMetaData = GraphBuilder.CreateSRV(PhysicalPageMetaDataRDG);

		PassParameters->IndirectArgs = PagesForHZBIndirectArgsRDG;
		PassParameters->PhysicalPagesForHZB = GraphBuilder.CreateSRV(PhysicalPagesForHZBRDG);
		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FVirtualSmBuildHZBPerPageCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildHZBPerPage"),
			ComputeShader,
			PassParameters,
			PassParameters->IndirectArgs,
			0
		);
	}
	{
		FVirtualSmBBuildHZBPerPageTopCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVirtualSmBBuildHZBPerPageTopCS::FParameters>();

		PassParameters->VirtualShadowMap = GetUncachedUniformBuffer(GraphBuilder);

		uint32 StartDestMip = FVirtualSmBuildHZBPerPageCS::HZBLevelsBase;
		for (int32 DestMip = 0; DestMip < FVirtualSmBBuildHZBPerPageTopCS::HZBLevelsTop; DestMip++)
		{
			PassParameters->FurthestHZBArrayOutput[DestMip] = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(HZBPhysicalArrayRDG, StartDestMip + DestMip));
		}
		FIntPoint SrcSize = FIntPoint::DivideAndRoundUp(FIntPoint(HZBPhysicalArrayRDG->Desc.GetSize().X, HZBPhysicalArrayRDG->Desc.GetSize().Y), 1 << int32(StartDestMip - 1));
		PassParameters->InvHzbInputSize = FVector2f(1.0f / SrcSize.X, 1.0f / SrcSize.Y);;
		PassParameters->ParentTextureArrayMip = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(HZBPhysicalArrayRDG, StartDestMip - 1));
		PassParameters->ParentTextureMipSampler = TStaticSamplerState<SF_Point>::GetRHI();

		PassParameters->IndirectArgs = PagesForHZBIndirectArgsRDG;
		PassParameters->PhysicalPagesForHZB = GraphBuilder.CreateSRV(PhysicalPagesForHZBRDG);
		auto ComputeShader = GetGlobalShaderMap(Scene.GetFeatureLevel())->GetShader<FVirtualSmBBuildHZBPerPageTopCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("BuildHZBPerPageTop"),
			ComputeShader,
			PassParameters,
			PassParameters->IndirectArgs,
			// NOTE: offset 4 to get second set of args in the buffer.
			4U * sizeof(uint32)
		);
	}
}


uint32 FVirtualShadowMapArray::AddRenderViews(const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap, const FViewInfo* CullingView, float LODScaleFactor, bool bSetHZBParams, bool bUpdateHZBMetaData, TArray<Nanite::FPackedView, SceneRenderingAllocator> &OutVirtualShadowViews)
{
	// TODO: Decide if this sort of logic belongs here or in Nanite (as with the mip level view expansion logic)
	// We're eventually going to want to snap/quantize these rectangles/positions somewhat so probably don't want it
	// entirely within Nanite, but likely makes sense to have some sort of "multi-viewport" notion in Nanite that can
	// handle both this and mips.
	// NOTE: There's still the additional VSM view logic that runs on top of this in Nanite too (see CullRasterize variant)
	Nanite::FPackedViewParams BaseParams;
	BaseParams.ViewRect = FIntRect(0, 0, FVirtualShadowMap::VirtualMaxResolutionXY, FVirtualShadowMap::VirtualMaxResolutionXY);
	BaseParams.HZBTestViewRect = BaseParams.ViewRect;
	BaseParams.RasterContextSize = GetPhysicalPoolSize();
	BaseParams.MaxPixelsPerEdgeMultipler = 1.0f / LODScaleFactor;
	BaseParams.PrevTargetLayerIndex = INDEX_NONE;
	BaseParams.TargetMipLevel = 0;
	BaseParams.TargetMipCount = 1;	// No mips for clipmaps
	BaseParams.Flags = 0u;

	if (Clipmap->GetLightSceneInfo().Proxy)
	{
		BaseParams.bUseLightingChannelMask = true;
		BaseParams.LightingChannelMask = Clipmap->GetLightSceneInfo().Proxy->GetLightingChannelMask();
	}

	const TSharedPtr<FVirtualShadowMapPerLightCacheEntry>& CacheEntry = Clipmap->GetCacheEntry();
	if (CacheEntry.IsValid())
	{
		CacheEntry->MarkRendered(Scene.GetFrameNumber());

		// Enable the force-uncaching to remove needless caching overhead if there is nothing to cache (the light is constantly invalidated).
		BaseParams.Flags |= CacheEntry->IsUncached() ? NANITE_VIEW_FLAG_UNCACHED : 0u;
	}
	
	if (bNonNaniteUseRadiusThreshold && (!CacheEntry.IsValid() || CacheEntry->IsUncached()))
	{
		BaseParams.Flags |= NANITE_VIEW_MIN_SCREEN_RADIUS_CULL;
		BaseParams.MinBoundsRadius = GMinScreenRadiusForShadowCaster;
	}

	Nanite::SetCullingViewOverrides(CullingView, BaseParams);

	for (int32 ClipmapLevelIndex = 0; ClipmapLevelIndex < Clipmap->GetLevelCount(); ++ClipmapLevelIndex)
	{
		int32 VirtualShadowMapId = Clipmap->GetVirtualShadowMapId(ClipmapLevelIndex);

		Nanite::FPackedViewParams Params = BaseParams;
		Params.TargetLayerIndex = VirtualShadowMapId;
		Params.ViewMatrices = Clipmap->GetViewMatrices(ClipmapLevelIndex);
		Params.PrevTargetLayerIndex = INDEX_NONE;
		Params.PrevViewMatrices = Params.ViewMatrices;

		if (CacheEntry)
		{
			FVirtualShadowMapCacheEntry& LevelEntry = CacheEntry->ShadowMapEntries[ClipmapLevelIndex];

			if (bSetHZBParams)
			{
				LevelEntry.SetHZBViewParams(Params);
			}

			// If we're going to generate a new HZB this frame, save the associated metadata
			if (bUpdateHZBMetaData)
			{
				FVirtualShadowMapHZBMetadata HZBMeta;
				HZBMeta.TargetLayerIndex = Params.TargetLayerIndex;
				HZBMeta.ViewMatrices = Params.ViewMatrices;
				HZBMeta.ViewRect = Params.ViewRect;
				LevelEntry.CurrentHZBMetadata = HZBMeta;
			}
		}

		Nanite::FPackedView View = Nanite::CreatePackedView(Params);
		OutVirtualShadowViews.Add(View);
	}

	return uint32(Clipmap->GetLevelCount());
}

uint32 FVirtualShadowMapArray::AddRenderViews(const FProjectedShadowInfo* ProjectedShadowInfo, TConstArrayView<FViewInfo> Views, float LODScaleFactor, bool bSetHZBParams, bool bUpdateHZBMetaData, bool bClampToNearPlane, TArray<Nanite::FPackedView, SceneRenderingAllocator>& OutVirtualShadowViews)
{
	// VSM supports only whole scene shadows, so those without a "DependentView" are local lights
	// For local lights the origin is the (inverse of) pre-shadow translation. 
	check(ProjectedShadowInfo->bWholeSceneShadow);

	if (ProjectedShadowInfo->VirtualShadowMapClipmap)
	{
		check(ProjectedShadowInfo->DependentView != nullptr);
		check(bClampToNearPlane);

		return AddRenderViews(ProjectedShadowInfo->VirtualShadowMapClipmap, ProjectedShadowInfo->DependentView, LODScaleFactor, bSetHZBParams, bUpdateHZBMetaData, OutVirtualShadowViews);
	}
	Nanite::FPackedViewParams BaseParams;
	BaseParams.ViewRect = ProjectedShadowInfo->GetOuterViewRect();
	BaseParams.HZBTestViewRect = BaseParams.ViewRect;
	BaseParams.RasterContextSize = GetPhysicalPoolSize();
	BaseParams.MaxPixelsPerEdgeMultipler = 1.0f / LODScaleFactor;
	BaseParams.PrevTargetLayerIndex = INDEX_NONE;
	BaseParams.TargetMipLevel = 0;
	BaseParams.TargetMipCount = FVirtualShadowMap::MaxMipLevels;
	// local lights enable distance cull by default
	BaseParams.Flags = NANITE_VIEW_FLAG_DISTANCE_CULL | (bClampToNearPlane ? 0u : NANITE_VIEW_FLAG_NEAR_CLIP);
	if (ProjectedShadowInfo->GetLightSceneInfo().Proxy)
	{
		BaseParams.bUseLightingChannelMask = true;
		BaseParams.LightingChannelMask = ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetLightingChannelMask();
	}

	// Local lights, select the view closest to the local light to get some kind of reasonable behavior for split screen.
	int32 ClosestCullingViewIndex = 0;
	{
		double MinDistanceSq = (Views[0].ShadowViewMatrices.GetViewOrigin() + ProjectedShadowInfo->PreShadowTranslation).SquaredLength();
		for (int Index = 1; Index < Views.Num(); ++Index)
		{
			FVector TestOrigin = Views[Index].ShadowViewMatrices.GetViewOrigin();
			double TestDistanceSq = (TestOrigin + ProjectedShadowInfo->PreShadowTranslation).SquaredLength();
			if (TestDistanceSq < MinDistanceSq)
			{
				ClosestCullingViewIndex = Index;
				MinDistanceSq = TestDistanceSq;
			}
		}
	}
	TSharedPtr<FVirtualShadowMapPerLightCacheEntry> CacheEntry = ProjectedShadowInfo->VirtualShadowMapPerLightCacheEntry;
	check(CacheEntry.IsValid())
	CacheEntry->MarkRendered(Scene.GetFrameNumber());

	if (bNonNaniteUseRadiusThreshold && CacheEntry->IsUncached())
	{
		BaseParams.Flags |= NANITE_VIEW_MIN_SCREEN_RADIUS_CULL;
		BaseParams.MinBoundsRadius = GMinScreenRadiusForShadowCaster;
	}
	BaseParams.Flags |= CacheEntry->IsUncached() ? NANITE_VIEW_FLAG_UNCACHED : 0U;

	Nanite::SetCullingViewOverrides(&Views[ClosestCullingViewIndex], BaseParams);
	int32 NumMaps = ProjectedShadowInfo->bOnePassPointLightShadow ? 6 : 1;
	for (int32 Index = 0; Index < NumMaps; ++Index)
	{
		int32 VirtualShadowMapId = ProjectedShadowInfo->VirtualShadowMapId + Index;

		Nanite::FPackedViewParams Params = BaseParams;
		Params.TargetLayerIndex = VirtualShadowMapId;
		Params.ViewMatrices = ProjectedShadowInfo->GetShadowDepthRenderingViewMatrices(Index, true);
		Params.RangeBasedCullingDistance = ProjectedShadowInfo->GetLightSceneInfo().Proxy->GetRadius();

		if (CacheEntry)
		{
			FVirtualShadowMapCacheEntry& LevelEntry = CacheEntry->ShadowMapEntries[Index];
			
			if (bSetHZBParams)
			{
				LevelEntry.SetHZBViewParams(Params);
			}

			// If we're going to generate a new HZB this frame, save the associated metadata
			if (bUpdateHZBMetaData)
			{
				FVirtualShadowMapHZBMetadata HZBMeta;
				HZBMeta.TargetLayerIndex = Params.TargetLayerIndex;
				HZBMeta.ViewMatrices = Params.ViewMatrices;
				HZBMeta.ViewRect = Params.ViewRect;
				LevelEntry.CurrentHZBMetadata = HZBMeta;
			}
		}

		OutVirtualShadowViews.Add(Nanite::CreatePackedView(Params));
	}

	return uint32(NumMaps);
}

#if !UE_BUILD_SHIPPING
class FDesaturatePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDesaturatePS);
	SHADER_USE_PARAMETER_STRUCT(FDesaturatePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDesaturatePS, "/Engine/Private/VirtualShadowMaps/Desaturate.usf", "DesaturatePS", SF_Pixel);

class FTonemapProjectionDebugTexturePS : public FVirtualShadowMapPageManagementShader
{
	DECLARE_GLOBAL_SHADER(FTonemapProjectionDebugTexturePS);
	SHADER_USE_PARAMETER_STRUCT(FTonemapProjectionDebugTexturePS, FVirtualShadowMapPageManagementShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DebugTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DebugTextureSampler)
		SHADER_PARAMETER(uint32, VisualizeModeId)
		SHADER_PARAMETER(int32, VisualizeVirtualShadowMapId)
		SHADER_PARAMETER(float, VisualizeNaniteOverdrawScale)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FTonemapProjectionDebugTexturePS, "/Engine/Private/VirtualShadowMaps/VirtualShadowMapDebug.usf", "TonemapProjectionDebugTexturePS", SF_Pixel);

extern int32 GNaniteVisualizeOverdrawScale;
#endif //!UE_BUILD_SHIPPING

FScreenPassTexture FVirtualShadowMapArray::AddVisualizePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, int32 ViewIndex, EVSMVisualizationPostPass Pass, FScreenPassTexture& SceneColor, FScreenPassRenderTarget& OverrideOutput)
{
	FScreenPassTexture Output = MoveTemp(SceneColor);

	auto FinalizeOutput = [](FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture& Output, FScreenPassRenderTarget& OverrideOutput) -> FScreenPassTexture&
	{
		if (OverrideOutput.IsValid())
		{
			AddDrawTexturePass(GraphBuilder, View, Output, OverrideOutput);
			return OverrideOutput;
		}

		return Output;
	};

#if !UE_BUILD_SHIPPING

	if (!IsAllocated() || DebugVisualizationOutput.IsEmpty())
	{
		return MoveTemp(FinalizeOutput(GraphBuilder, View, Output, OverrideOutput));
	}

	const FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
	if (!VisualizationData.IsActive())
	{
		return MoveTemp(FinalizeOutput(GraphBuilder, View, Output, OverrideOutput));
	}

	RDG_EVENT_SCOPE(GraphBuilder, "VirtualShadowMapsVisualization");

	FScreenPassTextureViewport InputViewport(DebugVisualizationOutput[ViewIndex]->Desc.Extent);
	FScreenPassTextureViewport OutputViewport(Output);
	FScreenPassRenderTarget OutputTarget(Output.Texture, OutputViewport.Rect, ERenderTargetLoadAction::ELoad);

	int ActiveModeId = VisualizationData.GetActiveModeID();
	int VisualizeVirtualShadowMapId = VisualizeLight[ViewIndex].GetVirtualShadowMapId();

	// Resize viewport for layout
	const int32 VisualizeLayout = CVarVisualizeLayout.GetValueOnRenderThread();
	{
		// See CVarVisualizeLayout documentation
		if (VisualizeLayout == 1)		// Thumbnail
		{
			const int32 TileWidth = View.UnscaledViewRect.Width() / 3;
			const int32 TileHeight = View.UnscaledViewRect.Height() / 3;

			OutputViewport.Rect.Max = OutputViewport.Rect.Min + FIntPoint(TileWidth, TileHeight);
		}
		else if (VisualizeLayout == 2)	// Split screen
		{
			InputViewport.Rect.Max.X = InputViewport.Rect.Min.X + (InputViewport.Rect.Width() / 2);
			OutputViewport.Rect.Max.X = OutputViewport.Rect.Min.X + (OutputViewport.Rect.Width() / 2);
		}
	}

	auto DrawDebugVisualizationOutput = [&]()
	{
		TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FTonemapProjectionDebugTexturePS> PixelShader(View.ShaderMap);

		FTonemapProjectionDebugTexturePS::FParameters* Parameters = GraphBuilder.AllocParameters<FTonemapProjectionDebugTexturePS::FParameters>();
		Parameters->DebugTexture = DebugVisualizationOutput[ViewIndex];
		// Point sampling as DebugTexture could have non-linear data
		Parameters->DebugTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->VisualizeModeId = ActiveModeId;
		Parameters->VisualizeVirtualShadowMapId = VisualizeVirtualShadowMapId;
		Parameters->VisualizeNaniteOverdrawScale = GNaniteVisualizeOverdrawScale;
		Parameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, ERenderTargetLoadAction::ENoAction);

		// Blend with scene color if fullscreen, use black background otherwise
		FRHIBlendState* BlendState = 
			VisualizeLayout == 0
			? TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI()
			: TStaticBlendState<>::GetRHI();
		FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();

		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DrawTexture"), View, OutputViewport, InputViewport, VertexShader, PixelShader, BlendState, DepthStencilState, Parameters, EScreenPassDrawFlags::None);
	};
	
	if (Pass == EVSMVisualizationPostPass::PreEditorPrimitives)
	{
		// Desaturate scene color
		{
			TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
			TShaderMapRef<FDesaturatePS> DesaturatePixelShader(View.ShaderMap);

			FRDGTextureRef SceneColorCopy = GraphBuilder.CreateTexture(SceneColor.Texture->Desc, TEXT("SceneColorCopy"));
			AddCopyTexturePass(GraphBuilder, SceneColor.Texture, SceneColorCopy);

			FDesaturatePS::FParameters* Parameters = GraphBuilder.AllocParameters<FDesaturatePS::FParameters>();
			Parameters->InputTexture = SceneColorCopy;
			Parameters->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, ERenderTargetLoadAction::ENoAction);
			AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Desaturate"), View, OutputViewport, OutputViewport, VertexShader, DesaturatePixelShader, Parameters, EScreenPassDrawFlags::None);
		}

		// Render stuff that blends in with scene
		if (ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_SHADOW_FACTOR
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_OR_MIP
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_VIRTUAL_PAGE
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_CACHED_PAGE
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_SMRT_RAY_COUNT
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_DIRTY_PAGE
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_GPU_INVALIDATED_PAGE
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_MERGED_PAGE
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_NANITE_OVERDRAW)
		{
			DrawDebugVisualizationOutput();
		}
	}
	else if (Pass == EVSMVisualizationPostPass::PostEditorPrimitives)
	{
		// Render stuff that is not part of scene, e.g. UI
		if (ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_CLIPMAP_VIRTUAL_SPACE
			|| ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_GENERAL_DEBUG)
		{
			DrawDebugVisualizationOutput();
		}

		AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Labels"), View, OutputTarget,
			[&VisualizeLight = VisualizeLight[ViewIndex], &OutputViewport = OutputViewport, VisualizeVirtualShadowMapId=VisualizeVirtualShadowMapId](FCanvas& Canvas)
			{
				const float DPIScale = Canvas.GetDPIScale();
				Canvas.SetBaseTransform(FMatrix(FScaleMatrix(DPIScale) * Canvas.CalcBaseTransform2D(Canvas.GetViewRect().Width(), Canvas.GetViewRect().Height())));

				auto DrawColorTile = [&](float X, float Y, float Width, float Height, const FLinearColor& Color)
				{
					Canvas.DrawTile(X/DPIScale, Y/DPIScale, Width/DPIScale, Height/DPIScale, 0, 0, 0, 0, Color);
				};

				auto DrawShadowedString = [&](float X, float Y, FStringView Text, const FLinearColor& Color = FLinearColor::White)
				{
					Canvas.DrawShadowedString(X / DPIScale, Y / DPIScale, Text, GetStatsFont(), Color);
				};

				const FVirtualShadowMapVisualizationData& VisualizationData = GetVirtualShadowMapVisualizationData();
				FString ModeName = VisualizationData.GetActiveModeName().ToString();
				int32 ActiveModeId = VisualizationData.GetActiveModeID();

				// Legend
				FVector2D LegendSize = FVector2D(300, 30);
				if (ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_SHADOW_FACTOR)		LegendSize.Y = 60;
				else if (ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_CACHED_PAGE)	LegendSize.Y = 100;

				FVector2D LegendPosition = FVector2D(OutputViewport.Rect.Min.X + 8, OutputViewport.Rect.Max.Y - LegendSize.Y - 100);
				DrawColorTile(LegendPosition.X, LegendPosition.Y, LegendSize.X, LegendSize.Y, FLinearColor(0.1f,0.1f,0.1f,0.8f));

				FString HeaderLabel = ModeName;
				if (VisualizeLight.IsValid())
				{
					HeaderLabel = FString::Printf(TEXT("%s (%s)"), *ModeName, *VisualizeLight.GetLightName());
				}

				DrawShadowedString(LegendPosition.X + 5, LegendPosition.Y + 5, HeaderLabel);

				if (ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_SHADOW_FACTOR)
				{
					DrawColorTile(LegendPosition.X + 5, LegendPosition.Y + 25, 10, 10, FLinearColor(1,1,0));
					DrawShadowedString(LegendPosition.X + 20, LegendPosition.Y + 22, TEXT("Lit"));
					DrawColorTile(LegendPosition.X + 5, LegendPosition.Y + 45, 10, 10, FLinearColor(0,0,1));
					DrawShadowedString(LegendPosition.X + 20, LegendPosition.Y + 42, TEXT("Shadow"));
				}
				else if (ActiveModeId == VIRTUAL_SHADOW_MAP_VISUALIZE_CACHED_PAGE) 
				{
					extern int32 GVisualizeCachedPagesOnly;
					if (VisualizeVirtualShadowMapId != INDEX_NONE || GVisualizeCachedPagesOnly)
					{
						DrawColorTile(LegendPosition.X + 5, LegendPosition.Y + 25, 10, 10, FLinearColor(0,1,0));
						DrawShadowedString(LegendPosition.X + 20, LegendPosition.Y + 22, TEXT("Cached"));
					}
					if (!GVisualizeCachedPagesOnly)
					{
						DrawColorTile(LegendPosition.X + 5, LegendPosition.Y + 45, 10, 10, FLinearColor(1,0,0));
						DrawShadowedString(LegendPosition.X + 20, LegendPosition.Y + 42, TEXT("All Invalidated"));
						DrawColorTile(LegendPosition.X + 5, LegendPosition.Y + 65, 10, 10, FLinearColor(0,0,1));
						DrawShadowedString(LegendPosition.X + 20, LegendPosition.Y + 62, TEXT("Dynamic Invalidated"));
						DrawColorTile(LegendPosition.X + 5, LegendPosition.Y + 85, 10, 10, FLinearColor(0.75,1,0));
						DrawShadowedString(LegendPosition.X + 20, LegendPosition.Y + 82, TEXT("Force cached"));
					}
				}
			});
	}

#endif //!UE_BUILD_SHIPPING

	return MoveTemp(FinalizeOutput(GraphBuilder, View, Output, OverrideOutput));
}

float FVirtualShadowMapArray::InterpolateResolutionBias(float BiasNonMoving, float BiasMoving, float LightMobilityFactor)
{
	return FMath::Lerp(BiasNonMoving, FMath::Max(BiasNonMoving, BiasMoving), LightMobilityFactor);
}
