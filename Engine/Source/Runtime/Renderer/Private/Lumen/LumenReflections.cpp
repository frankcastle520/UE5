// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenReflections.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SingleLayerWaterRendering.h"
#include "LumenTracingUtils.h"
#include "LumenFrontLayerTranslucency.h"

extern FLumenGatherCvarState GLumenGatherCvars;

static TAutoConsoleVariable<int> CVarLumenAllowReflections(
	TEXT("r.Lumen.Reflections.Allow"),
	1,
	TEXT("Whether to allow Lumen Reflections.  Lumen Reflections is enabled in the project settings, this cvar can only disable it."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int> GVarLumenReflectionsDownsampleFactor(
	TEXT("r.Lumen.Reflections.DownsampleFactor"),
	1,
	TEXT("Downsample factor from the main viewport to trace rays. This is the main performance control for the tracing part of the reflections."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionTraceMeshSDFs = 1;
FAutoConsoleVariableRef GVarLumenReflectionTraceMeshSDFs(
	TEXT("r.Lumen.Reflections.TraceMeshSDFs"),
	GLumenReflectionTraceMeshSDFs,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionsUseRadianceCache = 0;
FAutoConsoleVariableRef CVarLumenReflectionsUseRadianceCache(
	TEXT("r.Lumen.Reflections.RadianceCache"),
	GLumenReflectionsUseRadianceCache,
	TEXT("Whether to reuse Lumen's ScreenProbeGather Radiance Cache, when it is available.  When enabled, reflection rays from rough surfaces are shortened and distant lighting comes from interpolating from the Radiance Cache, speeding up traces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionRadianceCacheAngleThresholdScale = 1.0f;
FAutoConsoleVariableRef CVarLumenReflectionRadianceCacheAngleThresholdScale(
	TEXT("r.Lumen.Reflections.RadianceCache.AngleThresholdScale"),
	GLumenReflectionRadianceCacheAngleThresholdScale,
	TEXT("Controls when the Radiance Cache is used for distant lighting.  A value of 1 means only use the Radiance Cache when appropriate for the reflection cone, lower values are more aggressive."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionRadianceCacheReprojectionRadiusScale = 10.0f;
FAutoConsoleVariableRef CVarLumenReflectionRadianceCacheReprojectionRadiusScale(
	TEXT("r.Lumen.Reflections.RadianceCache.ReprojectionRadiusScale"),
	GLumenReflectionRadianceCacheReprojectionRadiusScale,
	TEXT("Scales the radius of the sphere around each Radiance Cache probe that is intersected for parallax correction when interpolating from the Radiance Cache."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarLumenReflectionMaxRoughnessToTrace(
	TEXT("r.Lumen.Reflections.MaxRoughnessToTrace"),
	-1.0f,
	TEXT("Max roughness value for which Lumen still traces dedicated reflection rays. Overrides Post Process Volume settings when set to anything >= 0."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarLumenReflectionMaxRoughnessToTraceClamp(
	TEXT("r.Lumen.Reflections.MaxRoughnessToTraceClamp"),
	1.0f,
	TEXT("Scalability clamp for max roughness value for which Lumen still traces dedicated reflection rays. Project and Post Process Volumes settings are clamped to this value. Useful for scalability."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarLumenReflectionsMaxRoughnessToTraceForFoliage(
	TEXT("r.Lumen.Reflections.MaxRoughnessToTraceForFoliage"),
	0.2f,
	TEXT("Max roughness value for which Lumen still traces dedicated reflection rays from foliage pixels. Where foliage pixel is a pixel with two sided or subsurface shading model."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionRoughnessFadeLength = .1f;
FAutoConsoleVariableRef GVarLumenReflectionRoughnessFadeLength(
	TEXT("r.Lumen.Reflections.RoughnessFadeLength"),
	GLumenReflectionRoughnessFadeLength,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionGGXSamplingBias = .1f;
FAutoConsoleVariableRef GVarLumenReflectionGGXSamplingBias(
	TEXT("r.Lumen.Reflections.GGXSamplingBias"),
	GLumenReflectionGGXSamplingBias,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionTemporalFilter = 1;
FAutoConsoleVariableRef CVarLumenReflectionTemporalFilter(
	TEXT("r.Lumen.Reflections.Temporal"),
	GLumenReflectionTemporalFilter,
	TEXT("Whether to use a temporal filter"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarLumenReflectionTemporalMaxFramesAccumulated(
	TEXT("r.Lumen.Reflections.Temporal.MaxFramesAccumulated"),
	12.0f,
	TEXT("Lower values cause the temporal filter to propagate lighting changes faster, but also increase flickering from noise."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarLumenReflectionsTemporalNeighborhoodClampScale(
	TEXT("r.Lumen.Reflections.Temporal.NeighborhoodClampScale"),
	1.0f,
	TEXT("Scales how permissive is neighborhood clamp. Higher values reduce noise, but also increase ghosting."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarLumenReflectionTemporalMaxRayDirections(
	TEXT("r.Lumen.Reflections.Temporal.MaxRayDirections"),
	1024,
	TEXT("Number of possible random directions per pixel."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

float GLumenReflectionHistoryDistanceThreshold = .03f;
FAutoConsoleVariableRef CVarLumenReflectionHistoryDistanceThreshold(
	TEXT("r.Lumen.Reflections.Temporal.DistanceThreshold"),
	GLumenReflectionHistoryDistanceThreshold,
	TEXT("World space distance threshold needed to discard last frame's lighting results.  Lower values reduce ghosting from characters when near a wall but increase flickering artifacts."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

TAutoConsoleVariable<int32> CVarLumenReflectionMaxRayIntensity(
	TEXT("r.Lumen.Reflections.MaxRayIntensity"),
	40.0f,
	TEXT("Clamps the maximum ray lighting intensity (with PreExposure) to reduce fireflies."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionSmoothBias = 0.0f;
FAutoConsoleVariableRef GVarLumenReflectionSmoothBias(
	TEXT("r.Lumen.Reflections.SmoothBias"),
	GLumenReflectionSmoothBias,
	TEXT("Values larger than 0 apply a global material roughness bias for Lumen Reflections, where 1 is fully mirror."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenReflectionScreenSpaceReconstruction = 1;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstruction(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction"),
	GLumenReflectionScreenSpaceReconstruction,
	TEXT("Whether to use the screen space BRDF reweighting reconstruction"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenReflectionScreenSpaceReconstructionNumSamples = 5;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstructionNumSamples(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.NumSamples"),
	GLumenReflectionScreenSpaceReconstructionNumSamples,
	TEXT("Number of samples to use for the screen space BRDF reweighting reconstruction"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarLumenReflectionScreenSpaceReconstructionKernelRadius(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.KernelRadius"),
	8.0f,
	TEXT("Screen space reflection filter kernel radius in pixels"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

float GLumenReflectionScreenSpaceReconstructionRoughnessScale = 1.0f;
FAutoConsoleVariableRef CVarLumenReflectionScreenSpaceReconstructionRoughnessScale(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.RoughnessScale"),
	GLumenReflectionScreenSpaceReconstructionRoughnessScale,
	TEXT("Values higher than 1 allow neighbor traces to be blurred together more aggressively, but is not physically correct."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarLumenReflectionScreenSpaceReconstructionTonemapMode(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.TonemapMode"),
	1,
	TEXT("Whether to tonemap reflection radiance values when they are averaged together during the spatial resolve. This reduces noise but also removes bright interesting features in reflections.\n")
	TEXT("0 - disabled. No bias, but also no noise reduction.\n")
	TEXT("1 - x^2. Good balance between bias and noise reduction. Recommended for full resolution reflections\n")
	TEXT("2 - 1/(1 + s*x). Heavy bias, but also removes a lot of noise. Recommended for downsampled reflections"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarLumenReflectionScreenSpaceReconstructionTonemapStrength(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.TonemapStrength"),
	1.0f,
	TEXT("Selected r.Lumen.Reflections.ScreenSpaceReconstruction.TonemapMode strength."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarLumenReflectionScreenSpaceReconstructionMinWeight(
	TEXT("r.Lumen.Reflections.ScreenSpaceReconstruction.MinWeight"),
	0.0f,
	TEXT("Min neighorhood weight adding some filtering even if we don't find good rays. It helps with noise on thin features when using downsampled tracing, but removes some contact shadows."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

int32 GLumenReflectionBilateralFilter = 1;
FAutoConsoleVariableRef CVarLumenReflectionBilateralFilter(
	TEXT("r.Lumen.Reflections.BilateralFilter"),
	GLumenReflectionBilateralFilter,
	TEXT("Whether to do a bilateral filter as a last step in denoising Lumen Reflections."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarLumenReflectionBilateralFilterKernelRadius(
	TEXT("r.Lumen.Reflections.BilateralFilter.KernelRadius"),
	8.0f,
	TEXT("Screen space reflection spatial filter kernel radius in pixels"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

int32 GLumenReflectionBilateralFilterNumSamples = 4;
FAutoConsoleVariableRef CVarLumenReflectionBilateralFilterNumSamples(
	TEXT("r.Lumen.Reflections.BilateralFilter.NumSamples"),
	GLumenReflectionBilateralFilterNumSamples,
	TEXT("Number of bilateral filter samples."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenReflectionBilateralFilterDepthWeightScale = 10000.0f;
FAutoConsoleVariableRef CVarLumenReflectionBilateralFilterDepthWeightScale(
	TEXT("r.Lumen.Reflections.BilateralFilter.DepthWeightScale"),
	GLumenReflectionBilateralFilterDepthWeightScale,
	TEXT("Scales the depth weight of the bilateral filter"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenReflectionsVisualizeTracingCoherency = 0;
FAutoConsoleVariableRef GVarLumenReflectionsVisualizeTracingCoherency(
	TEXT("r.Lumen.Reflections.VisualizeTracingCoherency"),
	GLumenReflectionsVisualizeTracingCoherency,
	TEXT("Set to 1 to capture traces from a random wavefront and draw them on the screen. Set to 1 again to re-capture.  Shaders must enable support first, see DEBUG_SUPPORT_VISUALIZE_TRACE_COHERENCY"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsAsyncCompute(
	TEXT("r.Lumen.Reflections.AsyncCompute"),
	0,
	TEXT("Whether to run Lumen reflection passes on the compute pipe if possible."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsSurfaceCacheFeedback(
	TEXT("r.Lumen.Reflections.SurfaceCacheFeedback"),
	1,
	TEXT("Whether to allow writing into virtual surface cache feedback buffer from reflection rays."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenReflectionsHiResSurface(
	TEXT("r.Lumen.Reflections.HiResSurface"),
	1,
	TEXT("Whether reflections should sample highest available surface data or use lowest res always resident pages."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenReflectionsSpecularScale = 1.f;
FAutoConsoleVariableRef GVarLumenReflectionsSpecularScale(
	TEXT("r.Lumen.Reflections.SpecularScale"),
	GLumenReflectionsSpecularScale,
	TEXT("Non-physically correct Lumen specular reflection scale. Recommended to keep at 1."),
	ECVF_RenderThreadSafe);

float GLumenReflectionsContrast = 1.f;
FAutoConsoleVariableRef GVarLumenReflectionsContrast(
	TEXT("r.Lumen.Reflections.Contrast"),
	GLumenReflectionsContrast,
	TEXT("Non-physically correct Lumen reflection contrast. Recommended to keep at 1."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int> GVarLumenReflectionsFixedStateFrameIndex(
	TEXT("r.Lumen.Reflections.FixedStateFrameIndex"),
	-1,
	TEXT("Whether to override View.StateFrameIndex for debugging Lumen Reflections."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> GVarLumenReflectionsDebug(
	TEXT("r.Lumen.Reflections.Debug"),
	0,
	TEXT("Whether to enabled debug mode, which prints various extra debug information from shaders."),
	ECVF_RenderThreadSafe);

float GetLumenReflectionSpecularScale()
{
	return FMath::Max(GLumenReflectionsSpecularScale, 0.f);
}

float GetLumenReflectionContrast()
{
	return FMath::Clamp(GLumenReflectionsContrast, 0.001f, 1.0f);
}

namespace LumenReflections
{
	int32 GetMaxFramesAccumulated()
	{
		return FMath::Max(CVarLumenReflectionTemporalMaxFramesAccumulated.GetValueOnRenderThread(), 1);
	}
};

bool LumenReflections::UseSurfaceCacheFeedback()
{
	return CVarLumenReflectionsSurfaceCacheFeedback.GetValueOnRenderThread() != 0;
}

bool LumenReflections::UseAsyncCompute(const FViewFamilyInfo& ViewFamily)
{
	return Lumen::UseAsyncCompute(ViewFamily) && CVarLumenReflectionsAsyncCompute.GetValueOnRenderThread() != 0;
}

void LumenReflections::SetupCompositeParameters(const FViewInfo& View, LumenReflections::FCompositeParameters& OutParameters)
{
	OutParameters.MaxRoughnessToTrace = FMath::Min(View.FinalPostProcessSettings.LumenMaxRoughnessToTraceReflections, CVarLumenReflectionMaxRoughnessToTraceClamp.GetValueOnRenderThread());
	OutParameters.InvRoughnessFadeLength = 1.0f / FMath::Clamp(GLumenReflectionRoughnessFadeLength, 0.001f, 1.0f);
	OutParameters.MaxRoughnessToTraceForFoliage = CVarLumenReflectionsMaxRoughnessToTraceForFoliage.GetValueOnRenderThread();

	if (CVarLumenReflectionMaxRoughnessToTrace.GetValueOnRenderThread() >= 0.0f)
	{
		OutParameters.MaxRoughnessToTrace = CVarLumenReflectionMaxRoughnessToTrace.GetValueOnRenderThread();
	}
}

TRefCountPtr<FRDGPooledBuffer> GVisualizeReflectionTracesData;

FRDGBufferRef SetupVisualizeReflectionTraces(FRDGBuilder& GraphBuilder, FLumenReflectionsVisualizeTracesParameters& VisualizeTracesParameters)
{
	FRDGBufferRef VisualizeTracesData = nullptr;

	if (GVisualizeReflectionTracesData.IsValid())
	{
		VisualizeTracesData = GraphBuilder.RegisterExternalBuffer(GVisualizeReflectionTracesData);
	}

	const int32 VisualizeBufferNumElements = 32 * 3;

	if (!VisualizeTracesData || VisualizeTracesData->Desc.NumElements != VisualizeBufferNumElements)
	{
		VisualizeTracesData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), VisualizeBufferNumElements), TEXT("VisualizeTracesData"));
		AddClearUAVFloatPass(GraphBuilder, GraphBuilder.CreateUAV(VisualizeTracesData, PF_A32B32G32R32F), 0.0f);
	}

	VisualizeTracesParameters.VisualizeTraceCoherency = 0;
	VisualizeTracesParameters.RWVisualizeTracesData = GraphBuilder.CreateUAV(VisualizeTracesData, PF_A32B32G32R32F);

	if (GLumenReflectionsVisualizeTracingCoherency == 1)
	{
		GLumenReflectionsVisualizeTracingCoherency = 2;
		VisualizeTracesParameters.VisualizeTraceCoherency = 1;
	}

	return VisualizeTracesData;
}

void GetReflectionsVisualizeTracesBuffer(TRefCountPtr<FRDGPooledBuffer>& VisualizeTracesData)
{
	if (GVisualizeReflectionTracesData.IsValid() && GLumenReflectionsVisualizeTracingCoherency != 0)
	{
		VisualizeTracesData = GVisualizeReflectionTracesData;
	}
}

// Must match usf RESOLVE_TILE_SIZE
const int32 GReflectionResolveTileSize = 8;

class FReflectionTileClassificationMarkCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionTileClassificationMarkCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionTileClassificationMarkCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, RWDownsampledDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionClearTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionResolveTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionTracingTileIndirectArgs)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, RWResolveTileUsed)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenFrontLayerTranslucencyGBufferParameters, FrontLayerTranslucencyGBufferParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		RDG_BUFFER_ACCESS(TileIndirectBuffer, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	class FFrontLayerTranslucency : SHADER_PERMUTATION_BOOL("FRONT_LAYER_TRANSLUCENCY");
	class FOverflowTile : SHADER_PERMUTATION_BOOL("PERMUTATION_OVERFLOW_TILE");
	using FPermutationDomain = TShaderPermutationDomain<FFrontLayerTranslucency, FOverflowTile>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FOverflowTile>() && !Substrate::IsSubstrateEnabled())
		{
			return false;
		}
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionTileClassificationMarkCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionTileClassificationMarkCS", SF_Compute);


class FReflectionTileClassificationBuildListsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionTileClassificationBuildListsCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionTileClassificationBuildListsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionClearTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionClearTileData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionTileIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWReflectionTileData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<uint>, ResolveTileUsed)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER(FIntPoint, TileViewportDimensions)
		SHADER_PARAMETER(FIntPoint, ResolveTileViewportDimensions)
		RDG_BUFFER_ACCESS(TileIndirectBuffer, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FOverflow>() && !Substrate::IsSubstrateEnabled())
		{
			return false;
		}
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	class FSupportDownsample : SHADER_PERMUTATION_BOOL("SUPPORT_DOWNSAMPLE_FACTOR");
	class FOverflow : SHADER_PERMUTATION_BOOL("PERMUTATION_OVERFLOW_TILE");
	using FPermutationDomain = TShaderPermutationDomain<FSupportDownsample,FOverflow>;

	static int32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionTileClassificationBuildListsCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionTileClassificationBuildListsCS", SF_Compute);



class FReflectionGenerateRaysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FReflectionGenerateRaysCS)
	SHADER_USE_PARAMETER_STRUCT(FReflectionGenerateRaysCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWRayBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, RWDownsampledDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, RWRayTraceDistance)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, RadianceCacheAngleThresholdScale)
		SHADER_PARAMETER(float, GGXSamplingBias)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenFrontLayerTranslucencyGBufferParameters, FrontLayerTranslucencyGBufferParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ResolveIndirectArgsForRead)
	END_SHADER_PARAMETER_STRUCT()

	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	class FFrontLayerTranslucency : SHADER_PERMUTATION_BOOL("FRONT_LAYER_TRANSLUCENCY");
	using FPermutationDomain = TShaderPermutationDomain<FRadianceCache, FFrontLayerTranslucency>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FReflectionGenerateRaysCS, "/Engine/Private/Lumen/LumenReflections.usf", "ReflectionGenerateRaysCS", SF_Compute);

class FLumenReflectionResolveCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionResolveCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionResolveCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float3>, RWSpecularIndirect)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float>, RWSpecularIndirectDepth)
		SHADER_PARAMETER(uint32, NumSpatialReconstructionSamples)
		SHADER_PARAMETER(float, SpatialReconstructionKernelRadius)
		SHADER_PARAMETER(float, SpatialReconstructionRoughnessScale)
		SHADER_PARAMETER(float, SpatialReconstructionMinWeight)
		SHADER_PARAMETER(float, SpatialReconstructionTonemapMode)
		SHADER_PARAMETER(float, SpatialReconstructionTonemapStrength)
		SHADER_PARAMETER(float, InvSubstrateMaxClosureCount)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenFrontLayerTranslucencyGBufferParameters, FrontLayerTranslucencyGBufferParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	class FSpatialReconstruction : SHADER_PERMUTATION_BOOL("USE_SPATIAL_RECONSTRUCTION");
	class FFrontLayerTranslucency : SHADER_PERMUTATION_BOOL("FRONT_LAYER_TRANSLUCENCY");
	class FDownsampleFactor : SHADER_PERMUTATION_RANGE_INT("DOWNSAMPLE_FACTOR", 1, 2);
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FSpatialReconstruction, FFrontLayerTranslucency, FDownsampleFactor, FDebugMode>;

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FDebugMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}
		return FGlobalShader::ShouldPrecachePermutation(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionResolveCS, "/Engine/Private/Lumen/LumenReflectionResolve.usf", "LumenReflectionResolveCS", SF_Compute);

bool ShouldRenderLumenReflections(const FSceneView& View, bool bSkipTracingDataCheck, bool bSkipProjectCheck, bool bIncludeStandalone)
{
	const FScene* Scene = (const FScene*)View.Family->Scene;
	if (Scene)
	{
		return Lumen::IsLumenFeatureAllowedForView(Scene, View, bSkipTracingDataCheck, bSkipProjectCheck) 
			&& View.FinalPostProcessSettings.ReflectionMethod == EReflectionMethod::Lumen
			&& View.Family->EngineShowFlags.LumenReflections 
			&& CVarLumenAllowReflections.GetValueOnAnyThread()
			&& (ShouldRenderLumenDiffuseGI(Scene, View, bSkipTracingDataCheck, bSkipProjectCheck)
				// GRHISupportsRayTracingShaders is required for standalone Lumen Reflections because Lumen::LumenHardwareRayTracing::GetHitLightingMode forces hit lighting
				|| (bIncludeStandalone && Lumen::UseHardwareRayTracedReflections(*View.Family) && GRHISupportsRayTracingShaders))
			&& (bSkipTracingDataCheck || Lumen::UseHardwareRayTracedReflections(*View.Family) || Lumen::IsSoftwareRayTracingSupported());
	}
	
	return false;
}

FLumenReflectionTileParameters ReflectionTileClassification(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FMinimalSceneTextures& SceneTextures,
	const FLumenReflectionTracingParameters& ReflectionTracingParameters,
	const FLumenFrontLayerTranslucencyGBufferParameters* FrontLayerReflectionGBuffer,
	ERDGPassFlags ComputePassFlags)
{
	FLumenReflectionTileParameters ReflectionTileParameters;

	const bool bFrontLayer = FrontLayerReflectionGBuffer != nullptr;
	const FIntPoint EffectiveTextureResolution = bFrontLayer ? SceneTextures.Config.Extent : Substrate::GetSubstrateTextureResolution(View, SceneTextures.Config.Extent);
	const uint32 ClosureCount = bFrontLayer ? 1u : Substrate::GetSubstrateMaxClosureCount(View);

	const FIntPoint ResolveTileViewportDimensions(
		FMath::DivideAndRoundUp(View.ViewRect.Size().X, GReflectionResolveTileSize), 
		FMath::DivideAndRoundUp(View.ViewRect.Size().Y, GReflectionResolveTileSize));

	const FIntPoint ResolveTileBufferDimensions(
		FMath::DivideAndRoundUp(EffectiveTextureResolution.X, GReflectionResolveTileSize),
		FMath::DivideAndRoundUp(EffectiveTextureResolution.Y, GReflectionResolveTileSize));

	const int32 TracingTileSize = GReflectionResolveTileSize * ReflectionTracingParameters.ReflectionDownsampleFactor;

	const FIntPoint TracingTileViewportDimensions(
		FMath::DivideAndRoundUp(View.ViewRect.Size().X, TracingTileSize), 
		FMath::DivideAndRoundUp(View.ViewRect.Size().Y, TracingTileSize));

	const FIntPoint TracingTileBufferDimensions(
		FMath::DivideAndRoundUp(EffectiveTextureResolution.X, TracingTileSize),
		FMath::DivideAndRoundUp(EffectiveTextureResolution.Y, TracingTileSize));

	const int32 NumResolveTiles = ResolveTileBufferDimensions.X * ResolveTileBufferDimensions.Y * ClosureCount;
	const int32 NumTracingTiles = TracingTileBufferDimensions.X * TracingTileBufferDimensions.Y * ClosureCount;

	FRDGBufferRef ReflectionClearTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumResolveTiles), TEXT("Lumen.Reflections.ReflectionClearTileData"));
	FRDGBufferRef ReflectionResolveTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumResolveTiles), TEXT("Lumen.Reflections.ReflectionResolveTileData"));

	FRDGBufferRef ReflectionClearTileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflections.ReflectionClearTileIndirectArgs"));
	FRDGBufferRef ReflectionResolveTileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflections.ReflectionResolveTileIndirectArgs"));
	FRDGBufferRef ReflectionTracingTileIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflections.ReflectionTracingTileIndirectArgs"));

	FRDGTextureDesc ResolveTileUsedDesc = FRDGTextureDesc::Create2DArray(ResolveTileBufferDimensions, PF_R8_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount);
	FRDGTextureRef ResolveTileUsed = GraphBuilder.CreateTexture(ResolveTileUsedDesc, TEXT("Lumen.Reflections.ResolveTileUsed"));

	{
		FRDGTextureUAVRef RWDownsampledDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.DownsampledDepth), ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef RWReflectionClearTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionClearTileIndirectArgs, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef RWReflectionResolveTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionResolveTileIndirectArgs, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGBufferUAVRef RWReflectionTracingTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionTracingTileIndirectArgs, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef RWResolveTileUsed = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ResolveTileUsed), ERDGUnorderedAccessViewFlags::SkipBarrier);

		auto ReflectionTileClassificationMark = [&](bool bOverflow)
		{
			FReflectionTileClassificationMarkCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionTileClassificationMarkCS::FParameters>();
			PassParameters->RWDownsampledDepth = RWDownsampledDepth;
			PassParameters->RWReflectionClearTileIndirectArgs = RWReflectionClearTileIndirectArgs;
			PassParameters->RWReflectionResolveTileIndirectArgs = RWReflectionResolveTileIndirectArgs;
			PassParameters->RWReflectionTracingTileIndirectArgs = RWReflectionTracingTileIndirectArgs;
			PassParameters->RWResolveTileUsed = RWResolveTileUsed;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;

			if (FrontLayerReflectionGBuffer)
			{
				PassParameters->FrontLayerTranslucencyGBufferParameters = *FrontLayerReflectionGBuffer;
			}
		
			PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
			PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;

			FReflectionTileClassificationMarkCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FReflectionTileClassificationMarkCS::FOverflowTile>(bOverflow);
			PermutationVector.Set<FReflectionTileClassificationMarkCS::FFrontLayerTranslucency>(bFrontLayer);
			auto ComputeShader = View.ShaderMap->GetShader<FReflectionTileClassificationMarkCS>(PermutationVector);

			checkf(ResolveTileViewportDimensions.X > 0 && ResolveTileViewportDimensions.Y > 0, TEXT("FReflectionTileClassificationMarkCS needs non-zero dispatch to clear next pass's indirect args"));

			{
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("TileClassificationMark(%dx%d)", View.ViewRect.Size().X, View.ViewRect.Size().Y),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					FIntVector(ResolveTileViewportDimensions.X, ResolveTileViewportDimensions.Y, ClosureCount));
			}
		};

		ReflectionTileClassificationMark(false);
	}

	// Classification for reflection tiles
	auto ReflectionTileClassificationBuildLists = [&](bool bOverflow)
	{
		FReflectionTileClassificationBuildListsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionTileClassificationBuildListsCS::FParameters>();
		PassParameters->RWReflectionClearTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionClearTileIndirectArgs, PF_R32_UINT);
		PassParameters->RWReflectionClearTileData = GraphBuilder.CreateUAV(ReflectionClearTileData, PF_R32_UINT);
		PassParameters->RWReflectionTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionResolveTileIndirectArgs, PF_R32_UINT);
		PassParameters->RWReflectionTileData = GraphBuilder.CreateUAV(ReflectionResolveTileData, PF_R32_UINT);
		PassParameters->ResolveTileUsed = ResolveTileUsed;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
		PassParameters->TileViewportDimensions = ResolveTileViewportDimensions;
		PassParameters->ResolveTileViewportDimensions = ResolveTileViewportDimensions;
		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;

		FReflectionTileClassificationBuildListsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FReflectionTileClassificationBuildListsCS::FSupportDownsample>(false);
		PermutationVector.Set<FReflectionTileClassificationBuildListsCS::FOverflow>(bOverflow);
		auto ComputeShader = View.ShaderMap->GetShader<FReflectionTileClassificationBuildListsCS>(PermutationVector);

		if (bOverflow)
		{
			PassParameters->TileIndirectBuffer = View.SubstrateViewData.ClosureTilePerThreadDispatchIndirectBuffer;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TileClassificationBuildLists(Overflow)"),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				View.SubstrateViewData.ClosureTilePerThreadDispatchIndirectBuffer, 0u);
		}
		else
		{
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TileClassificationBuildLists"),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ResolveTileViewportDimensions, FReflectionTileClassificationBuildListsCS::GetGroupSize()));
		}
	};

	ReflectionTileClassificationBuildLists(false);
	if (Substrate::IsSubstrateEnabled() && !bFrontLayer)
	{
		ReflectionTileClassificationBuildLists(true);
	}

	// Classification for reflection 'tracing' tiles
	FRDGBufferRef ReflectionTracingTileData;
	if (ReflectionTracingParameters.ReflectionDownsampleFactor == 1)
	{
		ReflectionTracingTileIndirectArgs = ReflectionResolveTileIndirectArgs;
		ReflectionTracingTileData = ReflectionResolveTileData;
	}
	else
	{
		ReflectionTracingTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumTracingTiles), TEXT("Lumen.Reflections.ReflectionTracingTileData"));

		FReflectionTileClassificationBuildListsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionTileClassificationBuildListsCS::FParameters>();
		PassParameters->RWReflectionTileIndirectArgs = GraphBuilder.CreateUAV(ReflectionTracingTileIndirectArgs, PF_R32_UINT);
		PassParameters->RWReflectionTileData = GraphBuilder.CreateUAV(ReflectionTracingTileData, PF_R32_UINT);
		PassParameters->ResolveTileUsed = ResolveTileUsed;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
		PassParameters->TileViewportDimensions = TracingTileViewportDimensions;
		PassParameters->ResolveTileViewportDimensions = ResolveTileViewportDimensions;
		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;

		FReflectionTileClassificationBuildListsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FReflectionTileClassificationBuildListsCS::FSupportDownsample >(true);
		auto ComputeShader = View.ShaderMap->GetShader<FReflectionTileClassificationBuildListsCS>(PermutationVector);

		// When using downsampled tracing, dispatch for all layers rather using linear sparse set of tiles (i.e., ClosureTilePerThreadDispatchIndirectBuffer) 
		// for easing logic within the TileClassificationBuildList shader
		FIntVector DispatchCount = FComputeShaderUtils::GetGroupCount(TracingTileViewportDimensions, FReflectionTileClassificationBuildListsCS::GetGroupSize());
		DispatchCount.Z = ClosureCount;
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TileClassificationBuildTracingLists"),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			DispatchCount);
	}

	ReflectionTileParameters.ClearIndirectArgs = ReflectionClearTileIndirectArgs;
	ReflectionTileParameters.ResolveIndirectArgs = ReflectionResolveTileIndirectArgs;
	ReflectionTileParameters.TracingIndirectArgs = ReflectionTracingTileIndirectArgs;
	ReflectionTileParameters.ReflectionClearTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ReflectionClearTileData, PF_R32_UINT));
	ReflectionTileParameters.ReflectionResolveTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ReflectionResolveTileData, PF_R32_UINT));
	ReflectionTileParameters.ReflectionTracingTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ReflectionTracingTileData, PF_R32_UINT));
	return ReflectionTileParameters;
}

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionDenoiserParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenFrontLayerTranslucencyGBufferParameters, FrontLayerTranslucencyGBufferParameters)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
	SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTracingParameters, ReflectionTracingParameters)
	SHADER_PARAMETER(float, InvSubstrateMaxClosureCount)
END_SHADER_PARAMETER_STRUCT()

class FLumenReflectionDenoiserTemporalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionDenoiserTemporalCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionDenoiserTemporalCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionDenoiserParameters, DenoiserParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<float4>, ResolvedSpecularLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, ResolvedReflectionsDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<float4>, SpecularHistoryTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<UNORM float>, NumFramesAccumulatedHistoryTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepthHistory)
		SHADER_PARAMETER(FVector4f, HistoryScreenPositionScaleBias)
		SHADER_PARAMETER(FVector4f, HistoryUVMinMax)
		SHADER_PARAMETER(FVector4f, HistoryGatherUVMinMax)
		SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWSpecularAndSecondMoment)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<UNORM float>, RWNumFramesAccumulated)
		SHADER_PARAMETER(float, TemporalMaxFramesAccumulated)
		SHADER_PARAMETER(float, TemporalNeighborhoodClampScale)
		SHADER_PARAMETER(float, HistoryDistanceThreshold)
	END_SHADER_PARAMETER_STRUCT()

	class FValidHistory : SHADER_PERMUTATION_BOOL("PERMUTATION_VALID_HISTORY");
	class FDebug : SHADER_PERMUTATION_BOOL("PERMUTATION_DEBUG");
	using FPermutationDomain = TShaderPermutationDomain<FValidHistory, FDebug>;

	static int32 GetGroupSize()
	{
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FDebug>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}
		return FGlobalShader::ShouldPrecachePermutation(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionDenoiserTemporalCS, "/Engine/Private/Lumen/LumenReflectionDenoiserTemporal.usf", "LumenReflectionDenoiserTemporalCS", SF_Compute);

class FLumenReflectionDenoiserClearCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionDenoiserClearCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionDenoiserClearCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionTileParameters, ReflectionTileParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float3>, RWResolvedSpecular)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWSpecularAndSecondMoment)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionDenoiserClearCS, "/Engine/Private/Lumen/LumenReflectionDenoiserClear.usf", "LumenReflectionDenoiserClearCS", SF_Compute);

class FLumenReflectionDenoiserSpatialCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenReflectionDenoiserSpatialCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenReflectionDenoiserSpatialCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenReflectionDenoiserParameters, DenoiserParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWSpecularIndirectAccumulated)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<float3>, SpecularLightingAndSecondMomentTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<UNORM float>, NumFramesAccumulatedTexture)
		SHADER_PARAMETER(float, SpatialFilterDepthWeightScale)
		SHADER_PARAMETER(float, SpatialFilterKernelRadius)
		SHADER_PARAMETER(uint32, SpatialFilterNumSamples)
		SHADER_PARAMETER(float, TemporalMaxFramesAccumulated)
	END_SHADER_PARAMETER_STRUCT()

	class FFrontLayerTranslucency : SHADER_PERMUTATION_BOOL("FRONT_LAYER_TRANSLUCENCY");
	class FSpatialFilter : SHADER_PERMUTATION_BOOL("SPATIAL_FILTER");
	class FDebugMode : SHADER_PERMUTATION_BOOL("DEBUG_MODE");
	using FPermutationDomain = TShaderPermutationDomain<FFrontLayerTranslucency, FSpatialFilter, FDebugMode>;

	static int32 GetGroupSize()
	{	
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FDebugMode>())
		{
			return EShaderPermutationPrecacheRequest::NotPrecached;
		}
		return FGlobalShader::ShouldPrecachePermutation(Parameters);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenReflectionDenoiserSpatialCS, "/Engine/Private/Lumen/LumenReflectionDenoiserSpatial.usf", "LumenReflectionDenoiserSpatialCS", SF_Compute);


DECLARE_GPU_STAT(LumenReflections);

FRDGTextureRef FDeferredShadingSceneRenderer::RenderLumenReflections(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	FLumenSceneFrameTemporaries& FrameTemporaries,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& ScreenProbeRadianceCacheParameters,
	ELumenReflectionPass ReflectionPass,
	const FTiledReflection* ExternalTiledReflection,
	const FLumenFrontLayerTranslucencyGBufferParameters* FrontLayerReflectionGBuffer,
	ERDGPassFlags ComputePassFlags)
{
	const bool bDenoise = ReflectionPass == ELumenReflectionPass::Opaque || ReflectionPass == ELumenReflectionPass::FrontLayerTranslucency;
	const bool bFrontLayer = ReflectionPass == ELumenReflectionPass::FrontLayerTranslucency;
	const bool bSingleLayerWater = ReflectionPass == ELumenReflectionPass::SingleLayerWater;
	const EDiffuseIndirectMethod DiffuseIndirectMethod = GetViewPipelineState(View).DiffuseIndirectMethod;

	check(ShouldRenderLumenReflections(View));
	check(ReflectionPass != ELumenReflectionPass::FrontLayerTranslucency 
		|| (FrontLayerReflectionGBuffer && FrontLayerReflectionGBuffer->FrontLayerTranslucencySceneDepth->Desc.Extent == SceneTextures.Config.Extent));

	LumenRadianceCache::FRadianceCacheInterpolationParameters RadianceCacheParameters = ScreenProbeRadianceCacheParameters;
	RadianceCacheParameters.RadianceCacheInputs.ReprojectionRadiusScale = FMath::Clamp<float>(GLumenReflectionRadianceCacheReprojectionRadiusScale, 1.0f, 100000.0f);

	LLM_SCOPE_BYTAG(Lumen);
	RDG_EVENT_SCOPE_STAT(GraphBuilder, LumenReflections, "LumenReflections");
	RDG_GPU_STAT_SCOPE(GraphBuilder, LumenReflections);

	FLumenReflectionTracingParameters ReflectionTracingParameters;
	{
		LumenReflections::SetupCompositeParameters(View, ReflectionTracingParameters.ReflectionsCompositeParameters);
		ReflectionTracingParameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRHI();
		ReflectionTracingParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		uint32 StateFrameIndex = View.ViewState ? View.ViewState->GetFrameIndex() : 0;
		if (GVarLumenReflectionsFixedStateFrameIndex.GetValueOnRenderThread() >= 0)
		{
			StateFrameIndex = GVarLumenReflectionsFixedStateFrameIndex.GetValueOnRenderThread();
		}

		ReflectionTracingParameters.ReflectionsStateFrameIndex = StateFrameIndex;
		ReflectionTracingParameters.ReflectionsStateFrameIndexMod8 = StateFrameIndex % 8;
		ReflectionTracingParameters.ReflectionsRayDirectionFrameIndex = StateFrameIndex % FMath::Max(CVarLumenReflectionTemporalMaxRayDirections.GetValueOnRenderThread(), 1);
	}

	FRDGBufferRef VisualizeTracesData = nullptr;
	
	if (ReflectionPass == ELumenReflectionPass::Opaque)
	{
		VisualizeTracesData = SetupVisualizeReflectionTraces(GraphBuilder, ReflectionTracingParameters.VisualizeTracesParameters);
	}

	// Compute effective reflection downsampling factor. 
	const int32 UserDownsampleFactor = View.FinalPostProcessSettings.LumenReflectionQuality <= .25f ? 2 : 1;
	const int32 LumenReflectionDownsampleFactor = bDenoise ? FMath::Clamp(GVarLumenReflectionsDownsampleFactor.GetValueOnRenderThread() * UserDownsampleFactor, 1, 2) : 1;
	ReflectionTracingParameters.ReflectionDownsampleFactor = LumenReflectionDownsampleFactor;
	const FIntPoint ViewSize = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), (int32)ReflectionTracingParameters.ReflectionDownsampleFactor);
	FIntPoint BufferSize = FIntPoint::DivideAndRoundUp(SceneTextures.Config.Extent, (int32)ReflectionTracingParameters.ReflectionDownsampleFactor);
	if (!bFrontLayer && !bSingleLayerWater)
	{
		BufferSize = Substrate::GetSubstrateTextureResolution(View, BufferSize);
	}
	const uint32 ClosureCount = Substrate::GetSubstrateMaxClosureCount(View);

	const bool bUseFarField = LumenReflections::UseFarField(*View.Family);
	const float NearFieldMaxTraceDistance = Lumen::GetMaxTraceDistance(View);

	ReflectionTracingParameters.ReflectionTracingViewSize = ViewSize;
	ReflectionTracingParameters.ReflectionTracingBufferSize = BufferSize;
	ReflectionTracingParameters.ReflectionTracingBufferInvSize = FVector2f(1.0f) / BufferSize;
	ReflectionTracingParameters.MaxRayIntensity = CVarLumenReflectionMaxRayIntensity.GetValueOnRenderThread();
	ReflectionTracingParameters.ReflectionSmoothBias = GLumenReflectionSmoothBias;
	ReflectionTracingParameters.ReflectionPass = (uint32)ReflectionPass;
	ReflectionTracingParameters.UseJitter = bDenoise && GLumenReflectionTemporalFilter ? 1 : 0;
	ReflectionTracingParameters.UseHighResSurface = CVarLumenReflectionsHiResSurface.GetValueOnRenderThread() != 0 ? 1 : 0;
	ReflectionTracingParameters.MaxReflectionBounces = LumenReflections::GetMaxReflectionBounces(View);
	ReflectionTracingParameters.MaxRefractionBounces = LumenReflections::GetMaxRefractionBounces(View);
	ReflectionTracingParameters.NearFieldMaxTraceDistance = NearFieldMaxTraceDistance;
	ReflectionTracingParameters.FarFieldMaxTraceDistance = bUseFarField ? Lumen::GetFarFieldMaxTraceDistance() : NearFieldMaxTraceDistance;
	ReflectionTracingParameters.NearFieldMaxTraceDistanceDitherScale = Lumen::GetNearFieldMaxTraceDistanceDitherScale(bUseFarField);
	ReflectionTracingParameters.NearFieldSceneRadius = Lumen::GetNearFieldSceneRadius(View, bUseFarField);

	FRDGTextureDesc RayBufferDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
	ReflectionTracingParameters.RayBuffer = GraphBuilder.CreateTexture(RayBufferDesc, TEXT("Lumen.Reflections.ReflectionRayBuffer"));

	FRDGTextureDesc DownsampledDepthDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
	ReflectionTracingParameters.DownsampledDepth = GraphBuilder.CreateTexture(DownsampledDepthDesc, TEXT("Lumen.Reflections.ReflectionDownsampledDepth"));

	FRDGTextureDesc RayTraceDistanceDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R16_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
	ReflectionTracingParameters.RayTraceDistance = GraphBuilder.CreateTexture(RayTraceDistanceDesc, TEXT("Lumen.Reflections.RayTraceDistance"));

	FBlueNoise BlueNoise = GetBlueNoiseGlobalParameters();
	ReflectionTracingParameters.BlueNoise = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);

	FLumenReflectionTileParameters ReflectionTileParameters;
	
	// Use the external tile list if there is one from Single Layer Water
	// It has scrambled tile order due to atomics but avoids tile classification twice
	if (ExternalTiledReflection 
		&& ExternalTiledReflection->DispatchIndirectParametersBuffer
		&& ReflectionTracingParameters.ReflectionDownsampleFactor == 1
		&& ExternalTiledReflection->TileSize == GReflectionResolveTileSize)
	{
		ReflectionTileParameters.ReflectionClearTileData = nullptr;
		ReflectionTileParameters.ReflectionResolveTileData = ExternalTiledReflection->TileListDataBufferSRV;
		ReflectionTileParameters.ReflectionTracingTileData = ExternalTiledReflection->TileListDataBufferSRV;
		ReflectionTileParameters.ClearIndirectArgs = nullptr;
		ReflectionTileParameters.ResolveIndirectArgs = ExternalTiledReflection->DispatchIndirectParametersBuffer;
		ReflectionTileParameters.TracingIndirectArgs = ExternalTiledReflection->DispatchIndirectParametersBuffer;
	}
	else
	{
		ReflectionTileParameters = ReflectionTileClassification(GraphBuilder, View, SceneTextures, ReflectionTracingParameters, FrontLayerReflectionGBuffer, ComputePassFlags);
	}

	const bool bUseRadianceCache = GLumenReflectionsUseRadianceCache != 0 && RadianceCacheParameters.RadianceProbeIndirectionTexture != nullptr;

	{
		FReflectionGenerateRaysCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionGenerateRaysCS::FParameters>();
		PassParameters->RWRayBuffer = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.RayBuffer));
		PassParameters->RWDownsampledDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.DownsampledDepth));
		PassParameters->RWRayTraceDistance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.RayTraceDistance));
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
		PassParameters->RadianceCacheAngleThresholdScale = FMath::Clamp<float>(GLumenReflectionRadianceCacheAngleThresholdScale, .05f, 4.0f);
		PassParameters->GGXSamplingBias = GLumenReflectionGGXSamplingBias;
		PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
		PassParameters->ResolveIndirectArgsForRead = GraphBuilder.CreateSRV(ReflectionTileParameters.TracingIndirectArgs, PF_R32_UINT);
		if (FrontLayerReflectionGBuffer)
		{
			PassParameters->FrontLayerTranslucencyGBufferParameters = *FrontLayerReflectionGBuffer;
		}

		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);

		FReflectionGenerateRaysCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FReflectionGenerateRaysCS::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FReflectionGenerateRaysCS::FFrontLayerTranslucency>(FrontLayerReflectionGBuffer != nullptr);
		auto ComputeShader = View.ShaderMap->GetShader<FReflectionGenerateRaysCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GenerateRays MaxRoughnessToTrace:%.2f%s",
				ReflectionTracingParameters.ReflectionsCompositeParameters.MaxRoughnessToTrace,
				bUseRadianceCache ? TEXT(" RadianceCache") : TEXT("")),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			ReflectionTileParameters.TracingIndirectArgs,
			0);
	}

	FRDGTextureDesc TraceRadianceDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
	ReflectionTracingParameters.TraceRadiance = GraphBuilder.CreateTexture(TraceRadianceDesc, TEXT("Lumen.Reflections.TraceRadiance"));
	ReflectionTracingParameters.RWTraceRadiance = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.TraceRadiance));

	FRDGTextureDesc TraceHitDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
	ReflectionTracingParameters.TraceHit = GraphBuilder.CreateTexture(TraceHitDesc, TEXT("Lumen.Reflections.TraceHit"));
	ReflectionTracingParameters.RWTraceHit = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.TraceHit));

	// Hit lighting requires a few optional buffers
	if (LumenReflections::UseHitLighting(View, DiffuseIndirectMethod))
	{
		FRDGTextureDesc TraceMaterialIdDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R16_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
		ReflectionTracingParameters.TraceMaterialId = GraphBuilder.CreateTexture(TraceMaterialIdDesc, TEXT("Lumen.Reflections.TraceMaterialId"));
		ReflectionTracingParameters.RWTraceMaterialId = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.TraceMaterialId));

		FRDGTextureDesc TraceBookmarkDesc(FRDGTextureDesc::Create2DArray(ReflectionTracingParameters.ReflectionTracingBufferSize, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount));
		ReflectionTracingParameters.TraceBookmark = GraphBuilder.CreateTexture(TraceBookmarkDesc, TEXT("Lumen.Reflections.TraceBookmark"));
		ReflectionTracingParameters.RWTraceBookmark = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ReflectionTracingParameters.TraceBookmark));
	}

	const bool bTraceMeshObjects = GLumenReflectionTraceMeshSDFs != 0 
		&& Lumen::UseMeshSDFTracing(ViewFamily.EngineShowFlags)
		// HZB is only built to include opaque but is used to cull Mesh SDFs
		&& ReflectionPass == ELumenReflectionPass::Opaque;

	TraceReflections(
		GraphBuilder, 
		Scene,
		View, 
		FrameTemporaries,
		bTraceMeshObjects,
		SceneTextures,
		ReflectionTracingParameters,
		ReflectionTileParameters,
		MeshSDFGridParameters,
		bUseRadianceCache,
		DiffuseIndirectMethod,
		RadianceCacheParameters,
		ComputePassFlags);
	
	if (VisualizeTracesData)
	{
		GVisualizeReflectionTracesData = GraphBuilder.ConvertToExternalBuffer(VisualizeTracesData);
	}

	const FIntPoint EffectiveTextureResolution = (bFrontLayer || bSingleLayerWater) ? SceneTextures.Config.Extent : Substrate::GetSubstrateTextureResolution(View, SceneTextures.Config.Extent);
	const FIntPoint EffectiveViewExtent = FrameTemporaries.ViewExtent;

	FRDGTextureRef ResolvedSpecularIndirect = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DArray(EffectiveTextureResolution, PF_FloatRGB, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount),
			bFrontLayer ? TEXT("Lumen.Reflections.FrontLayer.ResolvedSpecularIndirect") : TEXT("Lumen.Reflections.ResolvedSpecularIndirect"));

	FRDGTextureRef ResolvedSpecularIndirectDepth = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DArray(EffectiveTextureResolution, PF_R16F, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount),
			bFrontLayer ? TEXT("Lumen.Reflections.FrontLayer.ResolvedSpecularIndirectDepth") : TEXT("Lumen.Reflections.ResolvedSpecularIndirectDepth"));

	const int32 NumReconstructionSamples = FMath::Clamp(FMath::RoundToInt(View.FinalPostProcessSettings.LumenReflectionQuality * GLumenReflectionScreenSpaceReconstructionNumSamples), GLumenReflectionScreenSpaceReconstructionNumSamples, 64);
	const bool bUseSpatialReconstruction = bDenoise && GLumenReflectionScreenSpaceReconstruction != 0;
	const bool bUseBilaterialFilter = bDenoise && GLumenReflectionBilateralFilter != 0;

	FRDGTextureRef SpecularAndSecondMoment = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2DArray(View.GetSceneTexturesConfig().Extent, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount),
		bFrontLayer ? TEXT("Lumen.Reflections.FrontLayer.SpecularAndSecondMoment") : TEXT("Lumen.Reflections.SpecularAndSecondMoment"));

	FRDGTextureUAVRef ResolvedSpecularUAV = GraphBuilder.CreateUAV(ResolvedSpecularIndirect, ERDGUnorderedAccessViewFlags::SkipBarrier);

	// Clear tiles which won't be processed
	if (bDenoise)
	{
		FLumenReflectionDenoiserClearCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionDenoiserClearCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;
		PassParameters->RWResolvedSpecular = ResolvedSpecularUAV;
		PassParameters->RWSpecularAndSecondMoment = GraphBuilder.CreateUAV(SpecularAndSecondMoment);

		auto ComputeShader = View.ShaderMap->GetShader<FLumenReflectionDenoiserClearCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearEmptyTiles"),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			ReflectionTileParameters.ClearIndirectArgs,
			0);
	}

	// Resolve reflections
	{
		FLumenReflectionResolveCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionResolveCS::FParameters>();
		PassParameters->RWSpecularIndirect = ResolvedSpecularUAV;
		PassParameters->RWSpecularIndirectDepth = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ResolvedSpecularIndirectDepth));
		PassParameters->NumSpatialReconstructionSamples = NumReconstructionSamples;
		PassParameters->SpatialReconstructionKernelRadius = CVarLumenReflectionScreenSpaceReconstructionKernelRadius.GetValueOnRenderThread();
		PassParameters->SpatialReconstructionRoughnessScale = GLumenReflectionScreenSpaceReconstructionRoughnessScale;
		PassParameters->SpatialReconstructionMinWeight = FMath::Max(CVarLumenReflectionScreenSpaceReconstructionMinWeight.GetValueOnRenderThread(), 0.0f);
		PassParameters->SpatialReconstructionTonemapMode = FMath::Clamp(CVarLumenReflectionScreenSpaceReconstructionTonemapMode.GetValueOnRenderThread(), 0, 2);
		PassParameters->SpatialReconstructionTonemapStrength = FMath::Clamp(CVarLumenReflectionScreenSpaceReconstructionTonemapStrength.GetValueOnRenderThread(), 0.0f, 1.0f);
		PassParameters->ReflectionTracingParameters = ReflectionTracingParameters;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
		if (FrontLayerReflectionGBuffer)
		{
			PassParameters->FrontLayerTranslucencyGBufferParameters = *FrontLayerReflectionGBuffer;
		}
		PassParameters->ReflectionTileParameters = ReflectionTileParameters;
		PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
		PassParameters->InvSubstrateMaxClosureCount = 1.0f / ClosureCount;

		const bool bDebug = GVarLumenReflectionsDebug.GetValueOnRenderThread() != 0;
		if (bDebug)
		{
			ShaderPrint::SetEnabled(true);
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintUniformBuffer);
		}

		FLumenReflectionResolveCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenReflectionResolveCS::FSpatialReconstruction>(bUseSpatialReconstruction);
		PermutationVector.Set<FLumenReflectionResolveCS::FFrontLayerTranslucency>(FrontLayerReflectionGBuffer != nullptr);
		PermutationVector.Set<FLumenReflectionResolveCS::FDownsampleFactor>(LumenReflectionDownsampleFactor);
		PermutationVector.Set<FLumenReflectionResolveCS::FDebugMode>(bDebug);
		auto ComputeShader = View.ShaderMap->GetShader<FLumenReflectionResolveCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ReflectionsResolve DonwsampleFactor:%d", LumenReflectionDownsampleFactor),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			ReflectionTileParameters.ResolveIndirectArgs,
			0);
	}

	FRDGTextureRef SpecularIndirect = ResolvedSpecularIndirect;

	if (bDenoise)
	{
		FLumenReflectionDenoiserParameters DenoiserParameters;
		DenoiserParameters.ViewUniformBuffer = View.ViewUniformBuffer;
		DenoiserParameters.SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTextures.UniformBuffer);
		DenoiserParameters.SceneTexturesStruct = SceneTextures.UniformBuffer;
		if (FrontLayerReflectionGBuffer)
		{
			DenoiserParameters.FrontLayerTranslucencyGBufferParameters = *FrontLayerReflectionGBuffer;
		}
		DenoiserParameters.Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
		DenoiserParameters.ReflectionTileParameters = ReflectionTileParameters;
		DenoiserParameters.ReflectionTracingParameters = ReflectionTracingParameters;
		DenoiserParameters.InvSubstrateMaxClosureCount = 1.0f / ClosureCount;

		const bool bDebug = GVarLumenReflectionsDebug.GetValueOnRenderThread() != 0;
		if (bDebug)
		{
			ShaderPrint::SetEnabled(true);
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, DenoiserParameters.ShaderPrintUniformBuffer);
		}

		bool bTemporal = GLumenReflectionTemporalFilter != 0;
		bool bSpatial = GLumenReflectionBilateralFilter != 0;
		FVector4f HistoryScreenPositionScaleBias = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4f HistoryUVMinMax = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4f HistoryGatherUVMinMax = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		FRDGTextureRef SpecularAndSecondMomentHistory = nullptr;
		FRDGTextureRef NumFramesAccumulatedHistory = nullptr;

		FReflectionTemporalState* ReflectionState = nullptr;
		if (View.ViewState)
		{
			if (bFrontLayer)
			{
				ReflectionState = &View.ViewState->Lumen.TranslucentReflectionState;
			}
			else
			{
				ReflectionState = &View.ViewState->Lumen.ReflectionState;
			}
		}

		if (ReflectionState 
			&& !View.bCameraCut
			&& !View.bPrevTransformsReset
			&& bTemporal)
		{
			HistoryScreenPositionScaleBias = ReflectionState->HistoryScreenPositionScaleBias;
			HistoryUVMinMax = ReflectionState->HistoryUVMinMax;
			HistoryGatherUVMinMax = ReflectionState->HistoryGatherUVMinMax;

			if (ReflectionState->SpecularAndSecondMomentHistory
				&& ReflectionState->NumFramesAccumulatedHistory
				&& ReflectionState->SpecularAndSecondMomentHistory->GetDesc().Extent == View.GetSceneTexturesConfig().Extent)
			{
				SpecularAndSecondMomentHistory = GraphBuilder.RegisterExternalTexture(ReflectionState->SpecularAndSecondMomentHistory);
				NumFramesAccumulatedHistory = GraphBuilder.RegisterExternalTexture(ReflectionState->NumFramesAccumulatedHistory);
			}
		}

		FRDGTextureRef SceneDepthHistory = View.ViewState->StochasticLighting.SceneDepthHistory ? GraphBuilder.RegisterExternalTexture(View.ViewState->StochasticLighting.SceneDepthHistory) : nullptr;

		FRDGTextureRef NumFramesAccumulated = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DArray(View.GetSceneTexturesConfig().Extent, PF_G8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, ClosureCount),
			bFrontLayer ? TEXT("Lumen.Reflections.FrontLayer.NumFramesAccumulated") : TEXT("Lumen.Reflections.NumFramesAccumulated"));

		// Temporal accumulation
		{
			const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
			FRDGTextureRef VelocityTexture = GetIfProduced(SceneTextures.Velocity, SystemTextures.Black);

			FLumenReflectionDenoiserTemporalCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionDenoiserTemporalCS::FParameters>();
			PassParameters->DenoiserParameters = DenoiserParameters;
			PassParameters->ResolvedSpecularLighting = ResolvedSpecularIndirect;
			PassParameters->ResolvedReflectionsDepth = ResolvedSpecularIndirectDepth;
			PassParameters->SpecularHistoryTexture = SpecularAndSecondMomentHistory;
			PassParameters->NumFramesAccumulatedHistoryTexture = NumFramesAccumulatedHistory;
			PassParameters->VelocityTexture = VelocityTexture;
			PassParameters->SceneDepthHistory = SceneDepthHistory;
			PassParameters->PrevSceneColorPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;
			PassParameters->HistoryScreenPositionScaleBias = HistoryScreenPositionScaleBias;
			PassParameters->HistoryUVMinMax = HistoryUVMinMax;
			PassParameters->HistoryGatherUVMinMax = HistoryGatherUVMinMax;
			PassParameters->RWSpecularAndSecondMoment = GraphBuilder.CreateUAV(SpecularAndSecondMoment);
			PassParameters->RWNumFramesAccumulated = GraphBuilder.CreateUAV(NumFramesAccumulated);
			PassParameters->TemporalMaxFramesAccumulated = LumenReflections::GetMaxFramesAccumulated();
			PassParameters->TemporalNeighborhoodClampScale = CVarLumenReflectionsTemporalNeighborhoodClampScale.GetValueOnRenderThread();
			PassParameters->HistoryDistanceThreshold = GLumenReflectionHistoryDistanceThreshold;

			FLumenReflectionDenoiserTemporalCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionDenoiserTemporalCS::FValidHistory>(SceneDepthHistory != nullptr && SpecularAndSecondMomentHistory != nullptr && bTemporal);
			PermutationVector.Set<FLumenReflectionDenoiserTemporalCS::FDebug>(bDebug);
			auto ComputeShader = View.ShaderMap->GetShader<FLumenReflectionDenoiserTemporalCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TemporalAccumulation"),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				ReflectionTileParameters.ResolveIndirectArgs,
				0);
		}

		// Temporally accumulated specular history and luminance second moment
		SpecularIndirect = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DArray(EffectiveTextureResolution, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable, ClosureCount),
			bFrontLayer ? TEXT("Lumen.Reflections.FrontLayer.SpecularIndirect") : TEXT("Lumen.Reflections.SpecularIndirect"));

		// Spatial filter
		{
			FLumenReflectionDenoiserSpatialCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenReflectionDenoiserSpatialCS::FParameters>();
			PassParameters->DenoiserParameters = DenoiserParameters;
			PassParameters->RWSpecularIndirectAccumulated = GraphBuilder.CreateUAV(SpecularIndirect);
			PassParameters->SpecularLightingAndSecondMomentTexture = SpecularAndSecondMoment;
			PassParameters->NumFramesAccumulatedTexture = NumFramesAccumulated;
			PassParameters->SpatialFilterDepthWeightScale = GLumenReflectionBilateralFilterDepthWeightScale;
			PassParameters->SpatialFilterKernelRadius = CVarLumenReflectionBilateralFilterKernelRadius.GetValueOnRenderThread();
			PassParameters->SpatialFilterNumSamples = FMath::Clamp(GLumenReflectionBilateralFilterNumSamples, 0, 1024);
			PassParameters->TemporalMaxFramesAccumulated = LumenReflections::GetMaxFramesAccumulated();

			FLumenReflectionDenoiserSpatialCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenReflectionDenoiserSpatialCS::FFrontLayerTranslucency>(bFrontLayer);
			PermutationVector.Set<FLumenReflectionDenoiserSpatialCS::FSpatialFilter>(bSpatial);
			PermutationVector.Set<FLumenReflectionDenoiserSpatialCS::FDebugMode>(bDebug);
			auto ComputeShader = View.ShaderMap->GetShader<FLumenReflectionDenoiserSpatialCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Spatial"),
				ComputeShader,
				PassParameters,
				ReflectionTileParameters.ResolveIndirectArgs,
				0);
		}

		if (ReflectionState && !View.bStatePrevViewInfoIsReadOnly)
		{
			ReflectionState->HistoryFrameIndex = View.ViewState->PendingPrevFrameNumber;
			ReflectionState->HistoryViewRect = View.ViewRect;
			ReflectionState->HistoryScreenPositionScaleBias = View.GetScreenPositionScaleBias(View.GetSceneTexturesConfig().Extent, View.ViewRect);

			const FVector2D InvBufferSize(1.0f / SceneTextures.Config.Extent.X, 1.0f / SceneTextures.Config.Extent.Y);

			ReflectionState->HistoryUVMinMax = FVector4f(
				View.ViewRect.Min.X * InvBufferSize.X,
				View.ViewRect.Min.Y * InvBufferSize.Y,
				View.ViewRect.Max.X * InvBufferSize.X,
				View.ViewRect.Max.Y * InvBufferSize.Y);

			// Clamp gather4 to a valid bilinear footprint in order to avoid sampling outside of valid bounds
			ReflectionState->HistoryGatherUVMinMax = FVector4f(
				(View.ViewRect.Min.X + 0.51f) * InvBufferSize.X,
				(View.ViewRect.Min.Y + 0.51f) * InvBufferSize.Y,
				(View.ViewRect.Max.X - 0.51f) * InvBufferSize.X,
				(View.ViewRect.Max.Y - 0.51f) * InvBufferSize.Y);

			if (SpecularAndSecondMoment && NumFramesAccumulated && bTemporal)
			{
				GraphBuilder.QueueTextureExtraction(SpecularAndSecondMoment, &ReflectionState->SpecularAndSecondMomentHistory);
				GraphBuilder.QueueTextureExtraction(NumFramesAccumulated, &ReflectionState->NumFramesAccumulatedHistory);
			}
			else
			{
				ReflectionState->SpecularAndSecondMomentHistory = nullptr;
				ReflectionState->NumFramesAccumulatedHistory = nullptr;
			}
		}
	}

	return SpecularIndirect;
}

void Lumen::Shutdown()
{
	GVisualizeReflectionTracesData.SafeRelease();
}