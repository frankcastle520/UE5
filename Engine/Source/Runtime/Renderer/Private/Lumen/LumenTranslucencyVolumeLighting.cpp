// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenTranslucencyVolumeLighting.cpp
=============================================================================*/

#include "LumenTranslucencyVolumeLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "DistanceFieldLightingShared.h"
#include "LumenMeshCards.h"
#include "Math/Halton.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "LumenTracingUtils.h"
#include "LumenRadianceCache.h"

#if RHI_RAYTRACING

#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

#endif

TAutoConsoleVariable<int32> CVarLumenTranslucencyVolume(
	TEXT("r.Lumen.TranslucencyVolume.Enable"),
	1,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenTranslucencyVolumeTraceFromVolume(
	TEXT("r.Lumen.TranslucencyVolume.TraceFromVolume"),
	1,
	TEXT("Whether to ray trace from the translucency volume's voxels to gather indirect lighting.  Only makes sense to disable if TranslucencyVolume.RadianceCache is enabled."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyFroxelGridPixelSize(
	TEXT("r.Lumen.TranslucencyVolume.GridPixelSize"),
	32,
	TEXT("Size of a cell in the translucency grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyGridDistributionLogZScale(
	TEXT("r.Lumen.TranslucencyVolume.GridDistributionLogZScale"),
	.01f,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyGridDistributionLogZOffset(
	TEXT("r.Lumen.TranslucencyVolume.GridDistributionLogZOffset"),
	1.0f,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyGridDistributionZScale(
	TEXT("r.Lumen.TranslucencyVolume.GridDistributionZScale"),
	4.0f,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyGridEndDistanceFromCamera(
	TEXT("r.Lumen.TranslucencyVolume.EndDistanceFromCamera"),
	8000.0f,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeSpatialFilter(
	TEXT("r.Lumen.TranslucencyVolume.SpatialFilter"),
	1,
	TEXT("Whether to use a spatial filter on the volume traces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeSpatialFilterSampleCount(
	TEXT("r.Lumen.TranslucencyVolume.SpatialFilter.SampleCount"),
	3,
	TEXT("When r.Lumen.TranslucencyVolume.SpatialFilter.Mode=1, this controls the effective sample count of the separable filter; that will be SampleCount*2+1. Default to a [-3,3] filter of 7 sample."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeSpatialFilterStandardDeviation(
	TEXT("r.Lumen.TranslucencyVolume.SpatialFilter.StandardDeviation"),
	5.0f, // default to a flat filter
	TEXT("When r.Lumen.TranslucencyVolume.SpatialFilter.Mode=1, The standard deviation of the Gaussian filter in Pixel. If a large value, the filter will become a cube filter. While when getting closer to 0, the filter will become a sharper Gaussian filter. Default to 5 meaning not a sharp flilter, close to a box filter for the default SampleCount of 3."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeTemporalReprojection(
	TEXT("r.Lumen.TranslucencyVolume.TemporalReprojection"),
	1,
	TEXT("Whether to use temporal reprojection."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeJitter(
	TEXT("r.Lumen.TranslucencyVolume.Temporal.Jitter"),
	1,
	TEXT("Whether to apply jitter to each frame's translucency GI computation, achieving temporal super sampling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeHistoryWeight(
	TEXT("r.Lumen.TranslucencyVolume.Temporal.HistoryWeight"),
	0.9,
	TEXT("How much the history value should be weighted each frame.  This is a tradeoff between visible jittering and responsiveness."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

TAutoConsoleVariable<int32> CVarLumenTranslucencyVolumeTemporalMaxRayDirections(
	TEXT("r.Lumen.TranslucencyVolume.Temporal.MaxRayDirections"),
	8,
	TEXT("Number of possible random directions from froxel center when sampling the lumen scene."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeTraceStepFactor(
	TEXT("r.Lumen.TranslucencyVolume.TraceStepFactor"),
	2,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeTracingOctahedronResolution(
	TEXT("r.Lumen.TranslucencyVolume.TracingOctahedronResolution"),
	3,
	TEXT("Resolution of the tracing octahedron.  Determines how many traces are done per voxel of the translucency lighting volume."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeVoxelTraceStartDistanceScale(
	TEXT("r.Lumen.TranslucencyVolume.VoxelTraceStartDistanceScale"),
	1.0f,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeMaxRayIntensity(
	TEXT("r.Lumen.TranslucencyVolume.MaxRayIntensity"),
	20.0f,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarLumenTranslucencyVolumeRadianceCache(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache"),
	1,
	TEXT("Whether to use the Radiance Cache for Translucency"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheNumMipmaps(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.NumMipmaps"),
	3,
	TEXT("Number of radiance cache mipmaps."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenTranslucencyVolumeRadianceCacheClipmapWorldExtent(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.ClipmapWorldExtent"),
	2500.0f,
	TEXT("World space extent of the first clipmap"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenTranslucencyVolumeRadianceCacheClipmapDistributionBase(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.ClipmapDistributionBase"),
	2.0f,
	TEXT("Base of the Pow() that controls the size of each successive clipmap relative to the first."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheNumProbesToTraceBudget(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.NumProbesToTraceBudget"),
	200,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheGridResolution(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.GridResolution"),
	24,
	TEXT("Resolution of the probe placement grid within each clipmap"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheProbeResolution(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.ProbeResolution"),
	8,
	TEXT("Resolution of the probe's 2d radiance layout.  The number of rays traced for the probe will be ProbeResolution ^ 2"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheProbeAtlasResolutionInProbes(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.ProbeAtlasResolutionInProbes"),
	128,
	TEXT("Number of probes along one dimension of the probe atlas cache texture. This controls the memory usage of the cache. Overflow currently results in incorrect rendering. Aligned to the next power of two."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeRadianceCacheReprojectionRadiusScale(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.ReprojectionRadiusScale"),
	10.0f,
	TEXT(""),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheFarField(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.FarField"),
	0,
	TEXT("Whether to trace against the FarField representation"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarTranslucencyVolumeRadianceCacheStats(
	TEXT("r.Lumen.TranslucencyVolume.RadianceCache.Stats"),
	0,
	TEXT("GPU print out Radiance Cache update stats."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeGridCenterOffsetFromDepthBuffer(
	TEXT("r.Lumen.TranslucencyVolume.GridCenterOffsetFromDepthBuffer"),
	0.5f,
	TEXT("Offset in grid units to move grid center sample out form the depth buffer along the Z direction. -1 means disabled. This reduces sample self intersection with geometry when tracing the global distance field buffer, and thus reduces flickering in those areas, as well as results in less leaking sometimes. Set to -1 to disable."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarTranslucencyVolumeOffsetThresholdToAcceptDepthBufferOffset(
	TEXT("r.Lumen.TranslucencyVolume.OffsetThresholdToAcceptDepthBufferOffset"),
	1.0f,
	TEXT("Offset in grid units to accept a sample to be moved forward in front of the depth buffer. This is to avoid moving all samples behind the depth buffer forward which would affect the lighting of translucent and volumetric at edges of mesh. Default to 1.0 to only allow moving the first layer of froxel intersecting depth."),
	ECVF_RenderThreadSafe
);

namespace LumenTranslucencyVolume
{
	float GetEndDistanceFromCamera(const FViewInfo& View)
	{
		// Ideally we'd use LumenSceneViewDistance directly, but direct shadowing via translucency lighting volume only covers 5000.0f units by default (r.TranslucencyLightingVolumeOuterDistance), 
		//		so there isn't much point covering beyond that.  
		const float ViewDistanceScale = FMath::Clamp(View.FinalPostProcessSettings.LumenSceneViewDistance / 20000.0f, .1f, 100.0f);
		return FMath::Clamp<float>(CVarTranslucencyGridEndDistanceFromCamera.GetValueOnRenderThread() * ViewDistanceScale, 1.0f, 100000.0f);
	}
}

namespace LumenTranslucencyVolumeRadianceCache
{
	int32 GetNumClipmaps(float DistanceToCover)
	{
		int32 ClipmapIndex = 0;

		for (; ClipmapIndex < LumenRadianceCache::MaxClipmaps; ++ClipmapIndex)
		{
			const float ClipmapExtent = CVarLumenTranslucencyVolumeRadianceCacheClipmapWorldExtent.GetValueOnRenderThread() * FMath::Pow(CVarLumenTranslucencyVolumeRadianceCacheClipmapDistributionBase.GetValueOnRenderThread(), ClipmapIndex);

			if (ClipmapExtent > DistanceToCover)
			{
				break;
			}
		}

		return FMath::Clamp(ClipmapIndex + 1, 1, LumenRadianceCache::MaxClipmaps);
	}

	int32 GetClipmapGridResolution()
	{
		const int32 GridResolution = CVarTranslucencyVolumeRadianceCacheGridResolution.GetValueOnRenderThread();
		return FMath::Clamp(GridResolution, 1, 256);
	}

	int32 GetProbeResolution()
	{
		return CVarTranslucencyVolumeRadianceCacheProbeResolution.GetValueOnRenderThread();
	}

	int32 GetNumMipmaps()
	{
		return CVarTranslucencyVolumeRadianceCacheNumMipmaps.GetValueOnRenderThread();
	}

	int32 GetFinalProbeResolution()
	{
		return GetProbeResolution() + 2 * (1 << (GetNumMipmaps() - 1));
	}

	int32 GetProbeAtlasResolutionInProbes()
	{
		return FMath::RoundUpToPowerOfTwo(FMath::Clamp(CVarTranslucencyVolumeRadianceCacheProbeAtlasResolutionInProbes.GetValueOnRenderThread(), 1, 1024));
	}

	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs(const FViewInfo& View)
	{
		LumenRadianceCache::FRadianceCacheInputs Parameters = LumenRadianceCache::GetDefaultRadianceCacheInputs();
		Parameters.ReprojectionRadiusScale = CVarTranslucencyVolumeRadianceCacheReprojectionRadiusScale.GetValueOnRenderThread();
		Parameters.ClipmapWorldExtent = CVarLumenTranslucencyVolumeRadianceCacheClipmapWorldExtent.GetValueOnRenderThread();
		Parameters.ClipmapDistributionBase = CVarLumenTranslucencyVolumeRadianceCacheClipmapDistributionBase.GetValueOnRenderThread();
		Parameters.RadianceProbeClipmapResolution = GetClipmapGridResolution();
		Parameters.ProbeAtlasResolutionInProbes = FIntPoint(GetProbeAtlasResolutionInProbes(), GetProbeAtlasResolutionInProbes());
		Parameters.NumRadianceProbeClipmaps = GetNumClipmaps(LumenTranslucencyVolume::GetEndDistanceFromCamera(View));
		Parameters.RadianceProbeResolution = FMath::Max(GetProbeResolution(), LumenRadianceCache::MinRadianceProbeResolution);
		Parameters.FinalProbeResolution = GetFinalProbeResolution();
		Parameters.FinalRadianceAtlasMaxMip = GetNumMipmaps() - 1;
		const float TraceBudgetScale = View.Family->bCurrentlyBeingEdited ? 10.0f : 1.0f;
		Parameters.NumProbesToTraceBudget = CVarTranslucencyVolumeRadianceCacheNumProbesToTraceBudget.GetValueOnRenderThread() * TraceBudgetScale;
		Parameters.RadianceCacheStats = CVarTranslucencyVolumeRadianceCacheStats.GetValueOnRenderThread();

		// For translucent probes, we want to trace as close to the center as possible to get better GI in translucent and volumetric fog. Note that GLumenDiffuseMinTraceDistance is still applied.
		// So we reduce the probe TMin to a tiny value in order for the GI to better connect. Only done when TraceFromVolume is off since this one is connecting properly.
		Parameters.ProbeTMinScale = CVarLumenTranslucencyVolumeTraceFromVolume.GetValueOnRenderThread() != 0 ? 1 : 0.1;

		return Parameters;
	}
};

const static uint32 MaxTranslucencyVolumeConeDirections = 64;

FRDGTextureRef OrDefault2dTextureIfNull(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	return Texture ? Texture : GSystemTextures.GetBlackDummy(GraphBuilder);
}

FRDGTextureRef OrDefault2dArrayTextureIfNull(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
    return Texture ? Texture : GSystemTextures.GetBlackArrayDummy(GraphBuilder);
}

FRDGTextureRef OrDefault3dTextureIfNull(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	return Texture ? Texture : GSystemTextures.GetVolumetricBlackDummy(GraphBuilder);
}

FRDGTextureRef OrDefault3dUintTextureIfNull(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	return Texture ? Texture: GSystemTextures.GetVolumetricBlackUintDummy(GraphBuilder);
}

float GetLumenReflectionSpecularScale();
float GetLumenReflectionContrast();
FLumenTranslucencyLightingParameters GetLumenTranslucencyLightingParameters(
	FRDGBuilder& GraphBuilder, 
	const FLumenTranslucencyGIVolume& LumenTranslucencyGIVolume,
	const FLumenFrontLayerTranslucency& LumenFrontLayerTranslucency)
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	FLumenTranslucencyLightingParameters Parameters;
	Parameters.RadianceCacheInterpolationParameters = LumenTranslucencyGIVolume.RadianceCacheInterpolationParameters;

	if (!LumenTranslucencyGIVolume.RadianceCacheInterpolationParameters.RadianceCacheFinalRadianceAtlas)
	{
		Parameters.RadianceCacheInterpolationParameters.RadianceCacheInputs.FinalProbeResolution = 0;
	}

	Parameters.RadianceCacheInterpolationParameters.RadianceProbeIndirectionTexture = OrDefault3dUintTextureIfNull(GraphBuilder, Parameters.RadianceCacheInterpolationParameters.RadianceProbeIndirectionTexture);
	Parameters.RadianceCacheInterpolationParameters.RadianceCacheFinalRadianceAtlas = OrDefault2dTextureIfNull(GraphBuilder, Parameters.RadianceCacheInterpolationParameters.RadianceCacheFinalRadianceAtlas);
	Parameters.RadianceCacheInterpolationParameters.RadianceCacheFinalIrradianceAtlas = OrDefault2dTextureIfNull(GraphBuilder, Parameters.RadianceCacheInterpolationParameters.RadianceCacheFinalIrradianceAtlas);
	Parameters.RadianceCacheInterpolationParameters.RadianceCacheProbeOcclusionAtlas = OrDefault2dTextureIfNull(GraphBuilder, Parameters.RadianceCacheInterpolationParameters.RadianceCacheProbeOcclusionAtlas);
	Parameters.RadianceCacheInterpolationParameters.RadianceCacheDepthAtlas = OrDefault2dTextureIfNull(GraphBuilder, Parameters.RadianceCacheInterpolationParameters.RadianceCacheDepthAtlas);
	
	if (!Parameters.RadianceCacheInterpolationParameters.ProbeWorldOffset)
	{
		Parameters.RadianceCacheInterpolationParameters.ProbeWorldOffset = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVector4f))));
	}

	Parameters.FrontLayerTranslucencyReflectionParameters.Enabled = LumenFrontLayerTranslucency.bEnabled ? 1 : 0;
	Parameters.FrontLayerTranslucencyReflectionParameters.RelativeDepthThreshold = LumenFrontLayerTranslucency.RelativeDepthThreshold;
	Parameters.FrontLayerTranslucencyReflectionParameters.Radiance = OrDefault2dArrayTextureIfNull(GraphBuilder, LumenFrontLayerTranslucency.Radiance);
	Parameters.FrontLayerTranslucencyReflectionParameters.Normal = OrDefault2dTextureIfNull(GraphBuilder, LumenFrontLayerTranslucency.Normal);
	Parameters.FrontLayerTranslucencyReflectionParameters.SceneDepth = OrDefault2dTextureIfNull(GraphBuilder, LumenFrontLayerTranslucency.SceneDepth);
	Parameters.FrontLayerTranslucencyReflectionParameters.SpecularScale = GetLumenReflectionSpecularScale();
	Parameters.FrontLayerTranslucencyReflectionParameters.Contrast = GetLumenReflectionContrast();

	Parameters.TranslucencyGIVolume0            = LumenTranslucencyGIVolume.Texture0        ? LumenTranslucencyGIVolume.Texture0        : SystemTextures.VolumetricBlack;
	Parameters.TranslucencyGIVolume1            = LumenTranslucencyGIVolume.Texture1        ? LumenTranslucencyGIVolume.Texture1        : SystemTextures.VolumetricBlack;
	Parameters.TranslucencyGIVolumeHistory0     = LumenTranslucencyGIVolume.HistoryTexture0 ? LumenTranslucencyGIVolume.HistoryTexture0 : SystemTextures.VolumetricBlack;
	Parameters.TranslucencyGIVolumeHistory1     = LumenTranslucencyGIVolume.HistoryTexture1 ? LumenTranslucencyGIVolume.HistoryTexture1 : SystemTextures.VolumetricBlack;
	Parameters.TranslucencyGIVolumeSampler      = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters.TranslucencyGIGridZParams        = (FVector3f)LumenTranslucencyGIVolume.GridZParams;
	Parameters.TranslucencyGIGridPixelSizeShift = LumenTranslucencyGIVolume.GridPixelSizeShift;
	Parameters.TranslucencyGIGridSize           = LumenTranslucencyGIVolume.GridSize;
	return Parameters;
}

void GetTranslucencyGridZParams(float NearPlane, float FarPlane, FVector& OutZParams, int32& OutGridSizeZ)
{
	OutGridSizeZ = FMath::TruncToInt(FMath::Log2((FarPlane - NearPlane) * CVarTranslucencyGridDistributionLogZScale.GetValueOnRenderThread()) * CVarTranslucencyGridDistributionZScale.GetValueOnRenderThread()) + 1;
	OutZParams = FVector(CVarTranslucencyGridDistributionLogZScale.GetValueOnRenderThread(), CVarTranslucencyGridDistributionLogZOffset.GetValueOnRenderThread(), CVarTranslucencyGridDistributionZScale.GetValueOnRenderThread());
}

FVector TranslucencyVolumeTemporalRandom(uint32 FrameNumber)
{
	// Center of the voxel
	FVector RandomOffsetValue(.5f, .5f, .5f);

	if (CVarTranslucencyVolumeJitter.GetValueOnRenderThread())
	{
		RandomOffsetValue = FVector(Halton(FrameNumber & 1023, 2), Halton(FrameNumber & 1023, 3), Halton(FrameNumber & 1023, 5));
	}

	return RandomOffsetValue;
}


class FMarkRadianceProbesUsedByTranslucencyVolumeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMarkRadianceProbesUsedByTranslucencyVolumeCS)
	SHADER_USE_PARAMETER_STRUCT(FMarkRadianceProbesUsedByTranslucencyVolumeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheMarkParameters, RadianceCacheMarkParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeParameters, VolumeParameters)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMarkRadianceProbesUsedByTranslucencyVolumeCS, "/Engine/Private/Lumen/LumenTranslucencyVolumeLighting.usf", "MarkRadianceProbesUsedByTranslucencyVolumeCS", SF_Compute);


class FTranslucencyVolumeTraceVoxelsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTranslucencyVolumeTraceVoxelsCS)
	SHADER_USE_PARAMETER_STRUCT(FTranslucencyVolumeTraceVoxelsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, RWVolumeTraceRadiance)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWVolumeTraceHitDistance)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeParameters, VolumeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeTraceSetupParameters, TraceSetupParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FProbeSourceMode : SHADER_PERMUTATION_RANGE_INT("PROBE_SOURCE_MODE", 0, 2);
	class FTraceFromVolume : SHADER_PERMUTATION_BOOL("TRACE_FROM_VOLUME");
	class FSimpleCoverageBasedExpand : SHADER_PERMUTATION_BOOL("GLOBALSDF_SIMPLE_COVERAGE_BASED_EXPAND");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FProbeSourceMode, FTraceFromVolume, FSimpleCoverageBasedExpand>;

	static FIntVector GetGroupSize()
	{
		return FIntVector(8, 8, 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (!PermutationVector.Get<FTraceFromVolume>() && PermutationVector.Get<FSimpleCoverageBasedExpand>())
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTranslucencyVolumeTraceVoxelsCS, "/Engine/Private/Lumen/LumenTranslucencyVolumeLighting.usf", "TranslucencyVolumeTraceVoxelsCS", SF_Compute);


class FTranslucencyVolumeSpatialSeparableFilterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTranslucencyVolumeSpatialSeparableFilterCS)
	SHADER_USE_PARAMETER_STRUCT(FTranslucencyVolumeSpatialSeparableFilterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, RWVolumeTraceRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VolumeTraceRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VolumeTraceHitDistance)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeParameters, VolumeParameters)
		SHADER_PARAMETER(FVector3f, PreviousFrameJitterOffset)
		SHADER_PARAMETER(FMatrix44f, UnjitteredPrevWorldToClip)
		SHADER_PARAMETER(FIntVector3, SpatialFilterDirection)
		SHADER_PARAMETER(FVector3f, SpatialFilterGaussParams)
		SHADER_PARAMETER(int32, SpatialFilterSampleCount)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static FIntVector GetGroupSize()
	{
		return FIntVector(8, 8, 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTranslucencyVolumeSpatialSeparableFilterCS, "/Engine/Private/Lumen/LumenTranslucencyVolumeLighting.usf", "TranslucencyVolumeSpatialSeparableFilterCS", SF_Compute);


class FTranslucencyVolumeIntegrateCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTranslucencyVolumeIntegrateCS)
	SHADER_USE_PARAMETER_STRUCT(FTranslucencyVolumeIntegrateCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGI0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGI1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGINewHistory0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGINewHistory1)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingVolumeParameters, VolumeParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VolumeTraceRadiance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VolumeTraceHitDistance)
		SHADER_PARAMETER(float, HistoryWeight)
		SHADER_PARAMETER(FVector3f, PreviousFrameJitterOffset)
		SHADER_PARAMETER(FMatrix44f, UnjitteredPrevWorldToClip)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIHistory0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIHistory1)
		SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyGIHistorySampler)
	END_SHADER_PARAMETER_STRUCT()

	class FTemporalReprojection : SHADER_PERMUTATION_BOOL("USE_TEMPORAL_REPROJECTION");

	using FPermutationDomain = TShaderPermutationDomain<FTemporalReprojection>;

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTranslucencyVolumeIntegrateCS, "/Engine/Private/Lumen/LumenTranslucencyVolumeLighting.usf", "TranslucencyVolumeIntegrateCS", SF_Compute);

FLumenTranslucencyLightingVolumeParameters GetTranslucencyLightingVolumeParameters(const FViewInfo& View)
{
	const int32 TranslucencyFroxelGridPixelSize = FMath::Max(1, CVarTranslucencyFroxelGridPixelSize.GetValueOnRenderThread());
	const FIntPoint GridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), TranslucencyFroxelGridPixelSize);
	const float FarPlane = LumenTranslucencyVolume::GetEndDistanceFromCamera(View);
	const uint32 ViewStateFrameIndex = View.ViewState ? View.ViewState->GetFrameIndex() : 0;

	FVector ZParams;
	int32 GridSizeZ;
	GetTranslucencyGridZParams(View.NearClippingDistance, FarPlane, ZParams, GridSizeZ);

	const FIntVector TranslucencyGridSize(GridSizeXY.X, GridSizeXY.Y, FMath::Max(GridSizeZ, 1));

	FLumenTranslucencyLightingVolumeParameters Parameters;
	Parameters.TranslucencyGIGridZParams = (FVector3f)ZParams;
	Parameters.TranslucencyGIGridPixelSizeShift = FMath::FloorLog2(TranslucencyFroxelGridPixelSize);
	Parameters.TranslucencyGIGridSize = TranslucencyGridSize;

	Parameters.FrameJitterOffset = (FVector3f)TranslucencyVolumeTemporalRandom(ViewStateFrameIndex);
	Parameters.UnjitteredClipToTranslatedWorld = FMatrix44f(View.ViewMatrices.ComputeInvProjectionNoAAMatrix() * View.ViewMatrices.GetTranslatedViewMatrix().GetTransposed());		// LWC_TODO: Precision loss?
	Parameters.GridCenterOffsetFromDepthBuffer = CVarTranslucencyVolumeGridCenterOffsetFromDepthBuffer.GetValueOnRenderThread();
	Parameters.GridCenterOffsetThresholdToAcceptDepthBufferOffset = FMath::Max(0, CVarTranslucencyVolumeOffsetThresholdToAcceptDepthBufferOffset.GetValueOnRenderThread());
	Parameters.FroxelDirectionJitterFrameIndex = CVarTranslucencyVolumeJitter.GetValueOnRenderThread() ? int32(ViewStateFrameIndex % FMath::Max(1, CVarLumenTranslucencyVolumeTemporalMaxRayDirections.GetValueOnRenderThread())) : -1;

	Parameters.BlueNoise = CreateUniformBufferImmediate(GetBlueNoiseGlobalParameters(), EUniformBufferUsage::UniformBuffer_SingleDraw);
		
	Parameters.TranslucencyVolumeTracingOctahedronResolution = CVarTranslucencyVolumeTracingOctahedronResolution.GetValueOnRenderThread();
	
	Parameters.FurthestHZBTexture = View.HZB;
	Parameters.HZBMipLevel = FMath::Max<float>((int32)FMath::FloorLog2(TranslucencyFroxelGridPixelSize) - 1, 0.0f);
	Parameters.ViewportUVToHZBBufferUV = FVector2f(
		float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
		float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));

	return Parameters;
}

static void MarkRadianceProbesUsedByTranslucencyVolume(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FLumenTranslucencyLightingVolumeParameters VolumeParameters,
	const LumenRadianceCache::FRadianceCacheMarkParameters& RadianceCacheMarkParameters,
	ERDGPassFlags ComputePassFlags)
{
	FMarkRadianceProbesUsedByTranslucencyVolumeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMarkRadianceProbesUsedByTranslucencyVolumeCS::FParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->RadianceCacheMarkParameters = RadianceCacheMarkParameters;

	PassParameters->VolumeParameters = VolumeParameters;

	FMarkRadianceProbesUsedByTranslucencyVolumeCS::FPermutationDomain PermutationVector;
	auto ComputeShader = View.ShaderMap->GetShader<FMarkRadianceProbesUsedByTranslucencyVolumeCS>();

	const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(VolumeParameters.TranslucencyGIGridSize, FMarkRadianceProbesUsedByTranslucencyVolumeCS::GetGroupSize());

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MarkRadianceProbesUsedByTranslucencyVolume"),
		ComputePassFlags,
		ComputeShader,
		PassParameters,
		GroupSize);
}

void TraceVoxelsTranslucencyVolume(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	bool bDynamicSkyLight,
	const FLumenCardTracingParameters& TracingParameters,
	LumenRadianceCache::FRadianceCacheInterpolationParameters RadianceCacheParameters,
	FLumenTranslucencyLightingVolumeParameters VolumeParameters,
	FLumenTranslucencyLightingVolumeTraceSetupParameters TraceSetupParameters,
	FRDGTextureRef VolumeTraceRadiance,
	FRDGTextureRef VolumeTraceHitDistance,
	ERDGPassFlags ComputePassFlags)
{
	FTranslucencyVolumeTraceVoxelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTranslucencyVolumeTraceVoxelsCS::FParameters>();
	PassParameters->RWVolumeTraceRadiance = GraphBuilder.CreateUAV(VolumeTraceRadiance);
	PassParameters->RWVolumeTraceHitDistance = GraphBuilder.CreateUAV(VolumeTraceHitDistance);

	PassParameters->TracingParameters = TracingParameters;
	PassParameters->RadianceCacheParameters = RadianceCacheParameters;
	PassParameters->VolumeParameters = VolumeParameters;
	PassParameters->TraceSetupParameters = TraceSetupParameters;

	PassParameters->SceneTexturesStruct = View.GetSceneTextures().UniformBuffer;

	const bool bTraceFromVolume = CVarLumenTranslucencyVolumeTraceFromVolume.GetValueOnRenderThread() != 0;

	FTranslucencyVolumeTraceVoxelsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FTranslucencyVolumeTraceVoxelsCS::FDynamicSkyLight>(bDynamicSkyLight);
	PermutationVector.Set<FTranslucencyVolumeTraceVoxelsCS::FProbeSourceMode>(RadianceCacheParameters.RadianceProbeIndirectionTexture != nullptr ? 1 : 0);
	PermutationVector.Set<FTranslucencyVolumeTraceVoxelsCS::FTraceFromVolume>(bTraceFromVolume);
	PermutationVector.Set<FTranslucencyVolumeTraceVoxelsCS::FSimpleCoverageBasedExpand>(bTraceFromVolume && Lumen::UseGlobalSDFSimpleCoverageBasedExpand());
	auto ComputeShader = View.ShaderMap->GetShader<FTranslucencyVolumeTraceVoxelsCS>(PermutationVector);

	const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(VolumeTraceRadiance->Desc.GetSize(), FTranslucencyVolumeTraceVoxelsCS::GetGroupSize());

	const int32 TranslucencyVolumeTracingOctahedronResolution= CVarTranslucencyVolumeTracingOctahedronResolution.GetValueOnRenderThread();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s %ux%u", bTraceFromVolume ? TEXT("TraceVoxels") : TEXT("RadianceCacheInterpolate"), TranslucencyVolumeTracingOctahedronResolution, TranslucencyVolumeTracingOctahedronResolution),
		ComputePassFlags,
		ComputeShader,
		PassParameters,
		GroupSize);
}

LumenRadianceCache::FUpdateInputs FDeferredShadingSceneRenderer::GetLumenTranslucencyGIVolumeRadianceCacheInputs(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	ERDGPassFlags ComputePassFlags)
{
	const FLumenTranslucencyLightingVolumeParameters VolumeParameters = GetTranslucencyLightingVolumeParameters(View);
	const LumenRadianceCache::FRadianceCacheInputs RadianceCacheInputs = LumenTranslucencyVolumeRadianceCache::SetupRadianceCacheInputs(View);

	FRadianceCacheConfiguration Configuration;
	Configuration.bFarField = CVarTranslucencyVolumeRadianceCacheFarField.GetValueOnRenderThread() != 0;

	FMarkUsedRadianceCacheProbes MarkUsedRadianceCacheProbesCallbacks;

	if (CVarLumenTranslucencyVolume.GetValueOnRenderThread() && CVarLumenTranslucencyVolumeRadianceCache.GetValueOnRenderThread())
	{
		MarkUsedRadianceCacheProbesCallbacks.AddLambda([VolumeParameters, ComputePassFlags](
			FRDGBuilder& GraphBuilder, 
			const FViewInfo& View, 
			const LumenRadianceCache::FRadianceCacheMarkParameters& RadianceCacheMarkParameters)
			{
				MarkRadianceProbesUsedByTranslucencyVolume(
					GraphBuilder,
					View,
					VolumeParameters,
					RadianceCacheMarkParameters,
					ComputePassFlags);
			});
	}

	LumenRadianceCache::FUpdateInputs RadianceCacheUpdateInputs(
		RadianceCacheInputs,
		Configuration,
		View,
		nullptr,
		nullptr,
		FMarkUsedRadianceCacheProbes(),
		MoveTemp(MarkUsedRadianceCacheProbesCallbacks));

	return RadianceCacheUpdateInputs;
}

void FDeferredShadingSceneRenderer::ComputeLumenTranslucencyGIVolume(
	FRDGBuilder& GraphBuilder,
	FViewInfo& View, 
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	ERDGPassFlags ComputePassFlags)
{
	if (CVarLumenTranslucencyVolume.GetValueOnRenderThread())
	{
		RDG_EVENT_SCOPE(GraphBuilder, "TranslucencyVolumeLighting");

		const FMatrix44f UnjitteredPrevWorldToClip = FMatrix44f(View.PrevViewInfo.ViewMatrices.GetViewMatrix() * View.PrevViewInfo.ViewMatrices.ComputeProjectionNoAAMatrix());		// LWC_TODO: Precision loss?
		const EPixelFormat LightingDataFormat = Lumen::GetLightingDataFormat();

		if (CVarLumenTranslucencyVolumeRadianceCache.GetValueOnRenderThread() && !RadianceCacheParameters.RadianceProbeIndirectionTexture)
		{
			LumenRadianceCache::TInlineArray<LumenRadianceCache::FUpdateInputs> InputArray;
			LumenRadianceCache::TInlineArray<LumenRadianceCache::FUpdateOutputs> OutputArray;

			LumenRadianceCache::FUpdateInputs TranslucencyVolumeRadianceCacheUpdateInputs = GetLumenTranslucencyGIVolumeRadianceCacheInputs(
				GraphBuilder,
				View, 
				FrameTemporaries,
				ComputePassFlags);

			if (TranslucencyVolumeRadianceCacheUpdateInputs.IsAnyCallbackBound())
			{
				InputArray.Add(TranslucencyVolumeRadianceCacheUpdateInputs);
				OutputArray.Add(LumenRadianceCache::FUpdateOutputs(
					View.ViewState->Lumen.TranslucencyVolumeRadianceCacheState,
					RadianceCacheParameters));

				LumenRadianceCache::UpdateRadianceCaches(
					GraphBuilder, 
					FrameTemporaries,
					InputArray,
					OutputArray,
					Scene,
					ViewFamily,
					LumenCardRenderer.bPropagateGlobalLightingChange,
					ComputePassFlags);
			}
		}

		{
			FLumenCardTracingParameters TracingParameters;
			GetLumenCardTracingParameters(GraphBuilder, View, *Scene->GetLumenSceneData(View), FrameTemporaries, /*bSurfaceCacheFeedback*/ false, TracingParameters);

			const FLumenTranslucencyLightingVolumeParameters VolumeParameters = GetTranslucencyLightingVolumeParameters(View);
			const FIntVector TranslucencyGridSize = VolumeParameters.TranslucencyGIGridSize;

			FLumenTranslucencyLightingVolumeTraceSetupParameters TraceSetupParameters;
			{
				TraceSetupParameters.StepFactor = FMath::Clamp(CVarTranslucencyVolumeTraceStepFactor.GetValueOnRenderThread(), .1f, 10.0f);
				TraceSetupParameters.MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
				TraceSetupParameters.VoxelTraceStartDistanceScale = CVarTranslucencyVolumeVoxelTraceStartDistanceScale.GetValueOnRenderThread();
				TraceSetupParameters.MaxRayIntensity = CVarTranslucencyVolumeMaxRayIntensity.GetValueOnRenderThread();
			}

			const FIntVector OctahedralAtlasSize(
				TranslucencyGridSize.X * CVarTranslucencyVolumeTracingOctahedronResolution.GetValueOnRenderThread(),
				TranslucencyGridSize.Y * CVarTranslucencyVolumeTracingOctahedronResolution.GetValueOnRenderThread(),
				TranslucencyGridSize.Z);

			FRDGTextureDesc VolumeTraceRadianceDesc(FRDGTextureDesc::Create3D(OctahedralAtlasSize, LightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
			FRDGTextureDesc VolumeTraceHitDistanceDesc(FRDGTextureDesc::Create3D(OctahedralAtlasSize, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV));
	
			FRDGTextureRef VolumeTraceRadiance = GraphBuilder.CreateTexture(VolumeTraceRadianceDesc, TEXT("Lumen.TranslucencyVolume.VolumeTraceRadiance"));
			FRDGTextureRef VolumeTraceHitDistance = GraphBuilder.CreateTexture(VolumeTraceHitDistanceDesc, TEXT("Lumen.TranslucencyVolume.VolumeTraceHitDistance"));

			if (Lumen::UseHardwareRayTracedTranslucencyVolume(ViewFamily) && CVarLumenTranslucencyVolumeTraceFromVolume.GetValueOnRenderThread() != 0)
			{
				HardwareRayTraceTranslucencyVolume(
					GraphBuilder,
					View,
					TracingParameters,
					RadianceCacheParameters,
					VolumeParameters,
					TraceSetupParameters, 
					VolumeTraceRadiance, 
					VolumeTraceHitDistance,
					ComputePassFlags);
			}
			else
			{
				const bool bDynamicSkyLight = Lumen::ShouldHandleSkyLight(Scene, ViewFamily);
				TraceVoxelsTranslucencyVolume(
					GraphBuilder,
					View,
					bDynamicSkyLight,
					TracingParameters,
					RadianceCacheParameters,
					VolumeParameters,
					TraceSetupParameters,
					VolumeTraceRadiance,
					VolumeTraceHitDistance,
					ComputePassFlags);
			}

			if (CVarTranslucencyVolumeSpatialFilter.GetValueOnRenderThread())
			{
				for (int32 PassIndex = 0; PassIndex < 3; PassIndex++) // 3 passes for the separable filter , one for each axis
				{
					FRDGTextureRef FilteredVolumeTraceRadiance = GraphBuilder.CreateTexture(VolumeTraceRadianceDesc, TEXT("Lumen.TranslucencyVolume.FilteredVolumeTraceRadiance"));

					FTranslucencyVolumeSpatialSeparableFilterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTranslucencyVolumeSpatialSeparableFilterCS::FParameters>();
					PassParameters->RWVolumeTraceRadiance = GraphBuilder.CreateUAV(FilteredVolumeTraceRadiance);

					PassParameters->VolumeTraceRadiance = VolumeTraceRadiance;
					PassParameters->VolumeTraceHitDistance = VolumeTraceHitDistance;
					PassParameters->View = View.ViewUniformBuffer;
					PassParameters->VolumeParameters = VolumeParameters;

					const int32 PreviousFrameIndexOffset = View.bStatePrevViewInfoIsReadOnly ? 0 : 1;
					PassParameters->PreviousFrameJitterOffset = (FVector3f)TranslucencyVolumeTemporalRandom(View.ViewState ? View.ViewState->GetFrameIndex() - PreviousFrameIndexOffset : 0);
					PassParameters->UnjitteredPrevWorldToClip = UnjitteredPrevWorldToClip;

					PassParameters->SpatialFilterDirection = FIntVector3(PassIndex == 0 ? 1 : 0, PassIndex == 1 ? 1 : 0, PassIndex == 2 ? 1 : 0);
					PassParameters->SpatialFilterSampleCount = FMath::Max(1, CVarTranslucencyVolumeSpatialFilterSampleCount.GetValueOnRenderThread());

					const float GaussianFilterStandardDev = FMath::Max(0.1, CVarTranslucencyVolumeSpatialFilterStandardDeviation.GetValueOnRenderThread());
					PassParameters->SpatialFilterGaussParams = FVector3f(GaussianFilterStandardDev, 1.0f/(2.0f*GaussianFilterStandardDev*GaussianFilterStandardDev), 1.0/(GaussianFilterStandardDev*FMath::Sqrt(2.0f*PI)));

					FTranslucencyVolumeSpatialSeparableFilterCS::FPermutationDomain PermutationVector;
					auto ComputeShader = View.ShaderMap->GetShader<FTranslucencyVolumeSpatialSeparableFilterCS>(PermutationVector);

					const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(OctahedralAtlasSize, FTranslucencyVolumeSpatialSeparableFilterCS::GetGroupSize());

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("SpatialFilter"),
						ComputePassFlags,
						ComputeShader,
						PassParameters,
						GroupSize);

					VolumeTraceRadiance = FilteredVolumeTraceRadiance;
				}
			}

			FRDGTextureRef TranslucencyGIVolumeHistory0 = nullptr;
			FRDGTextureRef TranslucencyGIVolumeHistory1 = nullptr;

			if (View.ViewState && View.ViewState->Lumen.TranslucencyVolume0)
			{
				TranslucencyGIVolumeHistory0 = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.TranslucencyVolume0);
				TranslucencyGIVolumeHistory1 = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.TranslucencyVolume1);
			}

			FRDGTextureDesc LumenTranslucencyGIDesc0(FRDGTextureDesc::Create3D(TranslucencyGridSize, LightingDataFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));
			FRDGTextureDesc LumenTranslucencyGIDesc1(FRDGTextureDesc::Create3D(TranslucencyGridSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));
	
			FRDGTextureRef TranslucencyGIVolume0 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc0, TEXT("Lumen.TranslucencyVolume.SHLighting0"));
			FRDGTextureRef TranslucencyGIVolume1 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc1, TEXT("Lumen.TranslucencyVolume.SHLighting1"));
			FRDGTextureUAVRef TranslucencyGIVolume0UAV = GraphBuilder.CreateUAV(TranslucencyGIVolume0);
			FRDGTextureUAVRef TranslucencyGIVolume1UAV = GraphBuilder.CreateUAV(TranslucencyGIVolume1);

			FRDGTextureRef TranslucencyGIVolumeNewHistory0 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc0, TEXT("Lumen.TranslucencyVolume.SHLightingNewHistory0"));
			FRDGTextureRef TranslucencyGIVolumeNewHistory1 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc1, TEXT("Lumen.TranslucencyVolume.SHLightingNewHistory0"));
			FRDGTextureUAVRef TranslucencyGIVolumeNewHistory0UAV = GraphBuilder.CreateUAV(TranslucencyGIVolumeNewHistory0);
			FRDGTextureUAVRef TranslucencyGIVolumeNewHistory1UAV = GraphBuilder.CreateUAV(TranslucencyGIVolumeNewHistory1);

			{
				FTranslucencyVolumeIntegrateCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTranslucencyVolumeIntegrateCS::FParameters>();
				PassParameters->RWTranslucencyGI0 = TranslucencyGIVolume0UAV;
				PassParameters->RWTranslucencyGI1 = TranslucencyGIVolume1UAV;
				PassParameters->RWTranslucencyGINewHistory0 = TranslucencyGIVolumeNewHistory0UAV;
				PassParameters->RWTranslucencyGINewHistory1 = TranslucencyGIVolumeNewHistory1UAV;

				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->VolumeTraceRadiance = VolumeTraceRadiance;
				PassParameters->VolumeTraceHitDistance = VolumeTraceHitDistance;
				PassParameters->VolumeParameters = VolumeParameters;

				const bool bUseTemporalReprojection =
					CVarTranslucencyVolumeTemporalReprojection.GetValueOnRenderThread()
					&& View.ViewState
					&& !View.bCameraCut
					&& !View.bPrevTransformsReset
					&& ViewFamily.bRealtimeUpdate
					&& TranslucencyGIVolumeHistory0
					&& TranslucencyGIVolumeHistory0->Desc == LumenTranslucencyGIDesc0;

				PassParameters->HistoryWeight = CVarTranslucencyVolumeHistoryWeight.GetValueOnRenderThread();
				const int32 PreviousFrameIndexOffset = View.bStatePrevViewInfoIsReadOnly ? 0 : 1;
				PassParameters->PreviousFrameJitterOffset = (FVector3f)TranslucencyVolumeTemporalRandom(View.ViewState ? View.ViewState->GetFrameIndex() - PreviousFrameIndexOffset : 0);
				PassParameters->UnjitteredPrevWorldToClip = UnjitteredPrevWorldToClip;
				PassParameters->TranslucencyGIHistory0 = TranslucencyGIVolumeHistory0;
				PassParameters->TranslucencyGIHistory1 = TranslucencyGIVolumeHistory1;
				PassParameters->TranslucencyGIHistorySampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

				FTranslucencyVolumeIntegrateCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FTranslucencyVolumeIntegrateCS::FTemporalReprojection>(bUseTemporalReprojection);
				auto ComputeShader = View.ShaderMap->GetShader<FTranslucencyVolumeIntegrateCS>(PermutationVector);

				const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(TranslucencyGridSize, FTranslucencyVolumeIntegrateCS::GetGroupSize());

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Integrate %ux%ux%u", TranslucencyGridSize.X, TranslucencyGridSize.Y, TranslucencyGridSize.Z),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					GroupSize);
			}

			if (View.ViewState && !View.bStatePrevViewInfoIsReadOnly)
			{
				View.ViewState->Lumen.TranslucencyVolume0 = GraphBuilder.ConvertToExternalTexture(TranslucencyGIVolumeNewHistory0);
				View.ViewState->Lumen.TranslucencyVolume1 = GraphBuilder.ConvertToExternalTexture(TranslucencyGIVolumeNewHistory1);
			}

			View.GetOwnLumenTranslucencyGIVolume().Texture0 = TranslucencyGIVolume0;
			View.GetOwnLumenTranslucencyGIVolume().Texture1 = TranslucencyGIVolume1;

			View.GetOwnLumenTranslucencyGIVolume().HistoryTexture0 = TranslucencyGIVolumeNewHistory0;
			View.GetOwnLumenTranslucencyGIVolume().HistoryTexture1 = TranslucencyGIVolumeNewHistory1;

			View.GetOwnLumenTranslucencyGIVolume().GridZParams = (FVector)VolumeParameters.TranslucencyGIGridZParams;
			View.GetOwnLumenTranslucencyGIVolume().GridPixelSizeShift = FMath::FloorLog2(CVarTranslucencyFroxelGridPixelSize.GetValueOnRenderThread());
			View.GetOwnLumenTranslucencyGIVolume().GridSize = TranslucencyGridSize;
		}
	}
}
