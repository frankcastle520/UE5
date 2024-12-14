// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "LumenRadianceCache.h"
#include "LumenRadianceCacheInternal.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracing(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen radiance cache (Default = 1)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheTemporaryBufferAllocationDownsampleFactor(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.TemporaryBufferAllocationDownsampleFactor"),
	32,
	TEXT("Downsample factor on the temporary buffer used by Hardware Ray Tracing Radiance Cache.  Higher downsample factors save more transient allocator memory, but may cause overflow and artifacts."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenRadianceCacheHardwareRayTracingRetraceFarField(
	TEXT("r.Lumen.RadianceCache.HardwareRayTracing.Retrace.FarField"),
	1,
	TEXT("Determines whether a second trace will be fired for far-field contribution (Default = 1)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace Lumen
{
	bool UseHardwareRayTracedRadianceCache(const FSceneViewFamily& ViewFamily)
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing(ViewFamily)
			&& (CVarLumenRadianceCacheHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}
}

bool LumenRadianceCache::UseHitLighting(const FViewInfo& View, EDiffuseIndirectMethod DiffuseIndirectMethod)
{
	if (LumenHardwareRayTracing::IsRayGenSupported())
	{
		return LumenHardwareRayTracing::GetHitLightingMode(View, DiffuseIndirectMethod) == LumenHardwareRayTracing::EHitLightingMode::HitLighting;
	}

	return false;
}

#if RHI_RAYTRACING

namespace LumenRadianceCache
{
	static constexpr int32 MaxBatchSize = 2;

	enum class ERayTracingPass
	{
		Default,
		FarField,
		HitLighting,

		MAX
	};

	BEGIN_SHADER_PARAMETER_STRUCT(FRadianceCacheTracingParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, ProbeTraceData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ProbeTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, ProbeTraceTileData)
		SHADER_PARAMETER_ARRAY(FVector4f, RadianceProbeSettings, [LumenRadianceCache::MaxClipmaps])
		SHADER_PARAMETER(uint32, RadianceProbeResolution)
		SHADER_PARAMETER(uint32, ProbeAtlasResolutionModuloMask)
		SHADER_PARAMETER(uint32, ProbeAtlasResolutionDivideShift)
		SHADER_PARAMETER(uint32, FarField)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelData)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FBatchRadianceCacheTracingParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_ARRAY(FRadianceCacheTracingParameters, RadianceCache, [MaxBatchSize])
		SHADER_PARAMETER(uint32, TempAtlasNumTraceTiles)
	END_SHADER_PARAMETER_STRUCT()
}

class FLumenRadianceCacheHardwareRayTracing : public FLumenHardwareRayTracingShaderBase
{
	DECLARE_LUMEN_RAYTRACING_SHADER(FLumenRadianceCacheHardwareRayTracing)

	class FRayTracingPass : SHADER_PERMUTATION_ENUM_CLASS("RAY_TRACING_PASS", LumenRadianceCache::ERayTracingPass);
	class FSurfaceCacheAlphaMasking : SHADER_PERMUTATION_BOOL("SURFACE_CACHE_ALPHA_MASKING");
	class FRadianceCacheBatchSize : SHADER_PERMUTATION_RANGE_INT("RADIANCE_CACHE_BATCH_SIZE", 1, LumenRadianceCache::MaxBatchSize);
	using FPermutationDomain = TShaderPermutationDomain<FLumenHardwareRayTracingShaderBase::FBasePermutationDomain, FRayTracingPass, FSurfaceCacheAlphaMasking, FRadianceCacheBatchSize>;

	// Parameters
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWTraceRadianceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWTraceHitTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingShaderBase::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FBatchRadianceCacheTracingParameters, BatchTracingParameters)
		SHADER_PARAMETER(uint32, HitLightingShadowMode)
		SHADER_PARAMETER(uint32, HitLightingDirectLighting)
		SHADER_PARAMETER(uint32, HitLightingSkylight)
		SHADER_PARAMETER(float, FarFieldBias)
		SHADER_PARAMETER(float, NearFieldMaxTraceDistance)
		SHADER_PARAMETER(float, NearFieldSceneRadius)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)
	END_SHADER_PARAMETER_STRUCT()

	static uint32 GetGroupSize()
	{
		return LumenRadianceCache::TRACE_TILE_SIZE_2D;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingShaderBase::ModifyCompilationEnvironment(Parameters, ShaderDispatchType, Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FRayTracingPass>() == LumenRadianceCache::ERayTracingPass::Default)
		{
			OutEnvironment.SetDefine(TEXT("ENABLE_NEAR_FIELD_TRACING"), 1);
		}

		if (PermutationVector.Get<FRayTracingPass>() == LumenRadianceCache::ERayTracingPass::FarField)
		{
			OutEnvironment.SetDefine(TEXT("ENABLE_FAR_FIELD_TRACING"), 1);
		}
	}

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FRayTracingPass>() != LumenRadianceCache::ERayTracingPass::Default)
		{
			PermutationVector.Set<FSurfaceCacheAlphaMasking>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		if (ShaderDispatchType == Lumen::ERayTracingShaderDispatchType::Inline && PermutationVector.Get<FRayTracingPass>() == LumenRadianceCache::ERayTracingPass::HitLighting)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform)
			&& FLumenHardwareRayTracingShaderBase::ShouldCompilePermutation(Parameters, ShaderDispatchType);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		FPermutationDomain PermutationVector(PermutationId);
		if (PermutationVector.Get<FRayTracingPass>() == LumenRadianceCache::ERayTracingPass::HitLighting)
		{
			return ERayTracingPayloadType::RayTracingMaterial;
		}
		else
		{
			return ERayTracingPayloadType::LumenMinimal;
		}
	}
};

IMPLEMENT_LUMEN_RAYGEN_AND_COMPUTE_RAYTRACING_SHADERS(FLumenRadianceCacheHardwareRayTracing)

IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingRGS", SF_RayGen);

class FLumenRadianceCacheHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenRadianceCacheHardwareRayTracingIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV_ARRAY(Buffer<uint>, CompactedTraceTexelAllocator, [LumenRadianceCache::MaxBatchSize])
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWResolveIndirectArgs)
		SHADER_PARAMETER(FIntPoint, OutputThreadGroupSize)
	END_SHADER_PARAMETER_STRUCT()

	class FResolveIndirectArgs : SHADER_PERMUTATION_BOOL("RESOLVE_INDIRECT_ARGS");
	class FRadianceCacheBatchSize : SHADER_PERMUTATION_RANGE_INT("RADIANCE_CACHE_BATCH_SIZE", 1, LumenRadianceCache::MaxBatchSize);
	using FPermutationDomain = TShaderPermutationDomain<FResolveIndirectArgs, FRadianceCacheBatchSize>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenRadianceCacheHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "LumenRadianceCacheHardwareRayTracingIndirectArgsCS", SF_Compute);

class FSplatRadianceCacheIntoAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSplatRadianceCacheIntoAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FSplatRadianceCacheIntoAtlasCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D, RWRadianceProbeAtlasTexture, [LumenRadianceCache::MaxBatchSize])
		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D, RWDepthProbeAtlasTexture, [LumenRadianceCache::MaxBatchSize])
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FBatchRadianceCacheTracingParameters, BatchTracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceHitTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceRadianceTexture)
		RDG_BUFFER_ACCESS(ResolveIndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	class FRadianceCacheBatchSize : SHADER_PERMUTATION_RANGE_INT("RADIANCE_CACHE_BATCH_SIZE", 1, LumenRadianceCache::MaxBatchSize);
	using FPermutationDomain = TShaderPermutationDomain<FRadianceCacheBatchSize>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return LumenRadianceCache::TRACE_TILE_SIZE_2D;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FSplatRadianceCacheIntoAtlasCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "SplatRadianceCacheIntoAtlasCS", SF_Compute);

class FRadianceCacheCompactTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRadianceCacheCompactTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FRadianceCacheCompactTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(ResolveIndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV_ARRAY(RWBuffer<uint>, RWCompactedTraceTexelAllocator, [LumenRadianceCache::MaxBatchSize])
		SHADER_PARAMETER_RDG_BUFFER_UAV_ARRAY(RWBuffer<uint>, RWCompactedTraceTexelData, [LumenRadianceCache::MaxBatchSize])
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FBatchRadianceCacheTracingParameters, BatchTracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceHitTexture)
	END_SHADER_PARAMETER_STRUCT()

	class FRadianceCacheBatchSize : SHADER_PERMUTATION_RANGE_INT("RADIANCE_CACHE_BATCH_SIZE", 1, LumenRadianceCache::MaxBatchSize);
	using FPermutationDomain = TShaderPermutationDomain<FRadianceCacheBatchSize>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRadianceCacheCompactTracesCS, "/Engine/Private/Lumen/LumenRadianceCacheHardwareRayTracing.usf", "RadianceCacheCompactTracesCS", SF_Compute);

bool UseFarFieldForRadianceCache(const FSceneViewFamily& ViewFamily)
{
	return Lumen::UseFarField(ViewFamily) && CVarLumenRadianceCacheHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCache(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedRadianceCache(*View.Family) && LumenRadianceCache::UseHitLighting(View, GetViewPipelineState(View).DiffuseIndirectMethod))
	{
		for (int32 BatchSize = 1; BatchSize <= LumenRadianceCache::MaxBatchSize; ++BatchSize)
		{
			FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FRayTracingPass>(LumenRadianceCache::ERayTracingPass::HitLighting);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FRadianceCacheBatchSize>(BatchSize);
			PermutationVector = FLumenRadianceCacheHardwareRayTracingRGS::RemapPermutation(PermutationVector);

			TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingRadianceCacheLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedRadianceCache(*View.Family) && !Lumen::UseHardwareInlineRayTracing(*View.Family))
	{
		for (int32 BatchSize = 1; BatchSize <= LumenRadianceCache::MaxBatchSize; ++BatchSize)
		{
			// Default trace
			{
				FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FRayTracingPass>(LumenRadianceCache::ERayTracingPass::Default);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FRadianceCacheBatchSize>(BatchSize);
				PermutationVector = FLumenRadianceCacheHardwareRayTracingRGS::RemapPermutation(PermutationVector);

				TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);

				OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
			}

			if (UseFarFieldForRadianceCache(*View.Family))
			{
				FLumenRadianceCacheHardwareRayTracingRGS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FRayTracingPass>(LumenRadianceCache::ERayTracingPass::FarField);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingRGS::FRadianceCacheBatchSize>(BatchSize);
				PermutationVector = FLumenRadianceCacheHardwareRayTracingRGS::RemapPermutation(PermutationVector);

				TShaderRef<FLumenRadianceCacheHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingRGS>(PermutationVector);

				OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
			}
		}
	}
}

void DispatchRayGenOrComputeShader(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const FLumenCardTracingParameters& TracingParameters,
	LumenRadianceCache::FBatchRadianceCacheTracingParameters& BatchTracingParameters,
	const FLumenRadianceCacheHardwareRayTracing::FPermutationDomain& PermutationVector,
	EDiffuseIndirectMethod DiffuseIndirectMethod,
	bool bInlineRayTracing,
	bool bUseFarField,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FRDGTextureRef TraceRadianceTexture,
	FRDGTextureRef TraceHitTexture,
	ERDGPassFlags ComputePassFlags)
{
	FLumenRadianceCacheHardwareRayTracing::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracing::FParameters>();

	PassParameters->RWTraceRadianceTexture = GraphBuilder.CreateUAV(TraceRadianceTexture);
	PassParameters->RWTraceHitTexture = GraphBuilder.CreateUAV(TraceHitTexture);

	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		SceneTextures,
		View,
		TracingParameters,
		&PassParameters->SharedParameters);

	PassParameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	PassParameters->BatchTracingParameters = BatchTracingParameters;
	PassParameters->HitLightingShadowMode = LumenHardwareRayTracing::GetHitLightingShadowMode();
	PassParameters->HitLightingDirectLighting = LumenHardwareRayTracing::UseHitLightingDirectLighting() ? 1 : 0;
	PassParameters->HitLightingSkylight = LumenHardwareRayTracing::UseHitLightingSkylight(DiffuseIndirectMethod) ? 1 : 0;
	PassParameters->NearFieldMaxTraceDistance = PassParameters->BatchTracingParameters.IndirectTracingParameters.MaxTraceDistance;
	PassParameters->NearFieldSceneRadius = Lumen::GetNearFieldSceneRadius(View, bUseFarField);
	PassParameters->FarFieldBias = LumenHardwareRayTracing::GetFarFieldBias();
	PassParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	PassParameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
	PassParameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();

	const LumenRadianceCache::ERayTracingPass RayTracingPass = PermutationVector.Get<FLumenRadianceCacheHardwareRayTracing::FRayTracingPass>();
	const FString RayTracingPassName = RayTracingPass == LumenRadianceCache::ERayTracingPass::HitLighting ? TEXT("hit-lighting") : (RayTracingPass == LumenRadianceCache::ERayTracingPass::FarField ? TEXT("far-field") : TEXT("default"));

	const bool bUseMinimalPayload = RayTracingPass != LumenRadianceCache::ERayTracingPass::HitLighting;
	if (bInlineRayTracing && bUseMinimalPayload)
	{
		// Inline always runs as an indirect compute shader
		FLumenRadianceCacheHardwareRayTracingCS::AddLumenRayTracingDispatchIndirect(
			GraphBuilder,
			RDG_EVENT_NAME("HardwareRayTracingCS %s", *RayTracingPassName),
			View,
			PermutationVector,
			PassParameters,
			PassParameters->HardwareRayTracingIndirectArgs,
			0, 
			ComputePassFlags);
	}
	else
	{
		FLumenRadianceCacheHardwareRayTracingRGS::AddLumenRayTracingDispatchIndirect(
			GraphBuilder,
			RDG_EVENT_NAME("HardwareRayTracingRGS %s", *RayTracingPassName),
			View,
			PermutationVector,
			PassParameters,
			PassParameters->HardwareRayTracingIndirectArgs,
			0,
			bUseMinimalPayload);
	}	
}

#endif // RHI_RAYTRACING

extern int32 GRadianceCacheForceFullUpdate;

void LumenRadianceCache::RenderLumenHardwareRayTracingRadianceCache(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	const TInlineArray<FUpdateInputs>& InputArray,
	TInlineArray<FUpdateOutputs>& OutputArray,
	const TInlineArray<FRadianceCacheSetup>& SetupOutputArray,
	const TInlineArray<FRDGBufferRef>& ProbeTraceTileAllocatorArray,
	const TInlineArray<FRDGBufferRef>& ProbeTraceTileDataArray,
	const TInlineArray<FRDGBufferRef>& ProbeTraceDataArray,
	const TInlineArray<FRDGBufferRef>& HardwareRayTracingRayAllocatorBufferArray,
	const TInlineArray<FRDGBufferRef>& TraceProbesIndirectArgsArray,
	ERDGPassFlags ComputePassFlags)
{
#if RHI_RAYTRACING
	// Update multiple radiance caches at once in order to overlap work in a common case - single view with an opaque and an translucent radiance cache
	// Normal draw overlap doesn't work with our D3D12 RHI, so need to do it manually inside every dispatch
	for (int32 BaseRadianceCacheIndex = 0; BaseRadianceCacheIndex < InputArray.Num(); BaseRadianceCacheIndex += LumenRadianceCache::MaxBatchSize)
	{
		const FViewInfo& View = InputArray[BaseRadianceCacheIndex].View;
		const FSceneTextureParameters& SceneTextures = GetSceneTextureParameters(GraphBuilder, View);
		const uint32 BatchSize = FMath::Min(LumenRadianceCache::MaxBatchSize, InputArray.Num() - BaseRadianceCacheIndex);
		const EDiffuseIndirectMethod DiffuseIndirectMethod = EDiffuseIndirectMethod::Lumen;

		// Compute temporary atlas size
		// Overflow is possible however unlikely - only nearby probes trace at max resolution
		int32 TempAtlasNumTraceTiles = 0;
		const int32 TemporaryBufferAllocationDownsampleFactor = GRadianceCacheForceFullUpdate ? 4 : CVarLumenRadianceCacheTemporaryBufferAllocationDownsampleFactor.GetValueOnRenderThread();
		for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
		{
			const FUpdateInputs& Inputs = InputArray[BaseRadianceCacheIndex + IndexInBatch];
			const FRadianceCacheInputs& RadianceCacheInputs = Inputs.RadianceCacheInputs;

			const int32 MaxProbeTraceTileResolution = RadianceCacheInputs.RadianceProbeResolution / LumenRadianceCache::TRACE_TILE_SIZE_2D * 2;
			const int32 MaxNumProbes = RadianceCacheInputs.ProbeAtlasResolutionInProbes.X * RadianceCacheInputs.ProbeAtlasResolutionInProbes.Y;
			const int32 TempAtlasNumTraceTilesPerProbe = FMath::DivideAndRoundUp(MaxProbeTraceTileResolution * MaxProbeTraceTileResolution, TemporaryBufferAllocationDownsampleFactor);
			TempAtlasNumTraceTiles += MaxNumProbes * TempAtlasNumTraceTilesPerProbe;
		}

		FLumenCardTracingParameters TracingParameters;
		GetLumenCardTracingParameters(GraphBuilder, View, *Scene->GetLumenSceneData(View), FrameTemporaries, /*bSurfaceCacheFeedback*/ false, TracingParameters);

		LumenRadianceCache::FBatchRadianceCacheTracingParameters BatchTracingParameters;
		SetupLumenDiffuseTracingParametersForProbe(View, BatchTracingParameters.IndirectTracingParameters, /*DiffuseConeHalfAngle*/ -1.0f);
		BatchTracingParameters.TempAtlasNumTraceTiles = TempAtlasNumTraceTiles;

		bool bUseFarField = false;

		for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
		{
			const uint32 RadianceCacheIndex = BaseRadianceCacheIndex + IndexInBatch;

			FRadianceCacheTracingParameters& RadianceCacheParameters = BatchTracingParameters.RadianceCache[RadianceCacheIndex];
			RadianceCacheParameters.ProbeTraceData = GraphBuilder.CreateSRV(ProbeTraceDataArray[RadianceCacheIndex], PF_A32B32G32R32F);
			RadianceCacheParameters.CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(HardwareRayTracingRayAllocatorBufferArray[RadianceCacheIndex], PF_R32_UINT);
			RadianceCacheParameters.CompactedTraceTexelData = nullptr;
			RadianceCacheParameters.ProbeTraceTileAllocator = GraphBuilder.CreateSRV(ProbeTraceTileAllocatorArray[RadianceCacheIndex], PF_R32_UINT);
			RadianceCacheParameters.ProbeTraceTileData = GraphBuilder.CreateSRV(ProbeTraceTileDataArray[RadianceCacheIndex], PF_R32G32_UINT);

			const FUpdateInputs& Inputs = InputArray[RadianceCacheIndex];
			const FUpdateOutputs& Outputs = OutputArray[RadianceCacheIndex];
			const FRadianceCacheInterpolationParameters& InterpolationParameters = Outputs.RadianceCacheParameters;
			RadianceCacheParameters.ProbeAtlasResolutionModuloMask = InterpolationParameters.ProbeAtlasResolutionModuloMask;
			RadianceCacheParameters.ProbeAtlasResolutionDivideShift = InterpolationParameters.ProbeAtlasResolutionDivideShift;
			RadianceCacheParameters.RadianceProbeResolution = Inputs.RadianceCacheInputs.RadianceProbeResolution;
			RadianceCacheParameters.FarField = 0;

			if (UseFarFieldForRadianceCache(*View.Family) && Inputs.Configuration.bFarField)
			{
				RadianceCacheParameters.FarField = 1;
				bUseFarField = true;
			}

			for (uint32 ClipmapIndex = 0; ClipmapIndex < LumenRadianceCache::MaxClipmaps; ++ClipmapIndex)
			{
				RadianceCacheParameters.RadianceProbeSettings[ClipmapIndex] = InterpolationParameters.RadianceProbeSettings[ClipmapIndex];
			}
		}

		const FIntPoint WrappedTraceTileLayout(
			LumenRadianceCache::TRACE_TILE_ATLAS_STRITE_IN_TILES,
			FMath::DivideAndRoundUp(TempAtlasNumTraceTiles, LumenRadianceCache::TRACE_TILE_ATLAS_STRITE_IN_TILES));
		const FIntPoint TempTraceAtlasResolution = WrappedTraceTileLayout * LumenRadianceCache::TRACE_TILE_SIZE_2D;
		const EPixelFormat TraceRadianceTextureFormat = Lumen::GetLightingDataFormat();

		FRDGTextureRef TraceRadianceTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(TempTraceAtlasResolution, TraceRadianceTextureFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Lumen.RadianceCache.TraceRadiance"));

		FRDGTextureRef TraceHitTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(TempTraceAtlasResolution, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Lumen.RadianceCache.TraceHit"));

		const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing(*View.Family);
		const bool bUseHitLighting = LumenRadianceCache::UseHitLighting(View, DiffuseIndirectMethod);
		checkf(ComputePassFlags != ERDGPassFlags::AsyncCompute || bInlineRayTracing, TEXT("Async Lumen HWRT is only supported for inline ray tracing"));

		// Setup indirect parameters
		FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.RadianceCache.HardwareRayTracing.IndirectArgsBuffer"));
		FRDGBufferRef ResolveIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.RadianceCache.HardwareRayTracing.ResolveIndirectArgs"));
		{
			FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters>();
			for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
			{
				PassParameters->CompactedTraceTexelAllocator[IndexInBatch] = BatchTracingParameters.RadianceCache[IndexInBatch].CompactedTraceTexelAllocator;
			}
			PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer);
			PassParameters->RWResolveIndirectArgs = GraphBuilder.CreateUAV(ResolveIndirectArgs);
			PassParameters->OutputThreadGroupSize = bInlineRayTracing ? FLumenRadianceCacheHardwareRayTracingCS::GetThreadGroupSize(View.GetShaderPlatform()) : FLumenRadianceCacheHardwareRayTracingRGS::GetThreadGroupSize();

			FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FResolveIndirectArgs>(true);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FRadianceCacheBatchSize>(BatchSize);
			TShaderRef<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("HardwareRayTracingIndirectArgs BatchSize:%d", BatchSize),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		// Default tracing of near-field
		{
			FLumenRadianceCacheHardwareRayTracing::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracing::FRayTracingPass>(bUseHitLighting ? ERayTracingPass::HitLighting : ERayTracingPass::Default);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracing::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracing::FRadianceCacheBatchSize>(BatchSize);
			PermutationVector = FLumenRadianceCacheHardwareRayTracing::RemapPermutation(PermutationVector);

			DispatchRayGenOrComputeShader(
				GraphBuilder,
				Scene,
				View,
				SceneTextures,
				TracingParameters,
				BatchTracingParameters,
				PermutationVector,
				DiffuseIndirectMethod,
				bInlineRayTracing,
				bUseFarField,
				HardwareRayTracingIndirectArgsBuffer,
				TraceRadianceTexture,
				TraceHitTexture,
				ComputePassFlags);
		}

		if (bUseFarField)
		{
			TInlineArray<FRDGBufferRef> CompactedTraceTexelAllocatorArray(BatchSize);
			TInlineArray<FRDGBufferRef> CompactedTraceTexelDataArray(BatchSize);

			for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
			{
				CompactedTraceTexelAllocatorArray[IndexInBatch] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2), TEXT("Lumen.RadianceCache.CompactedTraceTexelAllocator"));;
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedTraceTexelAllocatorArray[IndexInBatch], PF_R32_UINT), 0, ComputePassFlags);

				const int32 NumCompactedTraceTexelDataElements = TempTraceAtlasResolution.X * TempTraceAtlasResolution.Y;
				CompactedTraceTexelDataArray[IndexInBatch] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCompactedTraceTexelDataElements), TEXT("Lumen.RadianceCache.CompactedTraceTexelData"));
			}

			// Compact unfinished traces
			{
				FRadianceCacheCompactTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRadianceCacheCompactTracesCS::FParameters>();
				for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
				{
					PassParameters->RWCompactedTraceTexelAllocator[IndexInBatch] = GraphBuilder.CreateUAV(CompactedTraceTexelAllocatorArray[IndexInBatch], PF_R32_UINT);
					PassParameters->RWCompactedTraceTexelData[IndexInBatch] = GraphBuilder.CreateUAV(CompactedTraceTexelDataArray[IndexInBatch], PF_R32_UINT);
				}
				PassParameters->ResolveIndirectArgs = ResolveIndirectArgs;
				PassParameters->BatchTracingParameters = BatchTracingParameters;
				PassParameters->TraceHitTexture = TraceHitTexture;

				FRadianceCacheCompactTracesCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FRadianceCacheCompactTracesCS::FRadianceCacheBatchSize>(BatchSize);
				auto ComputeShader = View.ShaderMap->GetShader<FRadianceCacheCompactTracesCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("CompactTraces"),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					PassParameters->ResolveIndirectArgs,
					0);
			}

			// Setup indirect parameters for the Far Field re-trace
			{
				FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FParameters>();
				for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
				{
					FRadianceCacheTracingParameters& RadianceCache = BatchTracingParameters.RadianceCache[IndexInBatch];
					RadianceCache.CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(CompactedTraceTexelAllocatorArray[IndexInBatch], PF_R32_UINT);
					RadianceCache.CompactedTraceTexelData = GraphBuilder.CreateSRV(CompactedTraceTexelDataArray[IndexInBatch], PF_R32_UINT);

					PassParameters->CompactedTraceTexelAllocator[IndexInBatch] = RadianceCache.CompactedTraceTexelAllocator;
				}
				PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer);
				PassParameters->RWResolveIndirectArgs = nullptr;
				PassParameters->OutputThreadGroupSize = bInlineRayTracing ? FLumenRadianceCacheHardwareRayTracingCS::GetThreadGroupSize(View.GetShaderPlatform()) : FLumenRadianceCacheHardwareRayTracingRGS::GetThreadGroupSize();

				FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FResolveIndirectArgs>(false);
				PermutationVector.Set<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS::FRadianceCacheBatchSize>(BatchSize);
				TShaderRef<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenRadianceCacheHardwareRayTracingIndirectArgsCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("HardwareRayTracingIndirectArgs FarField BatchSize:%d", BatchSize),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			FLumenRadianceCacheHardwareRayTracing::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracing::FRayTracingPass>(ERayTracingPass::FarField);
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracing::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
			PermutationVector.Set<FLumenRadianceCacheHardwareRayTracing::FRadianceCacheBatchSize>(BatchSize);
			PermutationVector = FLumenRadianceCacheHardwareRayTracing::RemapPermutation(PermutationVector);

			DispatchRayGenOrComputeShader(
				GraphBuilder,
				Scene,
				View,
				SceneTextures,
				TracingParameters,
				BatchTracingParameters,
				PermutationVector,
				DiffuseIndirectMethod,
				bInlineRayTracing,
				bUseFarField,
				HardwareRayTracingIndirectArgsBuffer,
				TraceRadianceTexture,
				TraceHitTexture,
				ComputePassFlags);
		}

		// Write temporary results to atlas, possibly up-sampling
		{
			FSplatRadianceCacheIntoAtlasCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSplatRadianceCacheIntoAtlasCS::FParameters>();
			PassParameters->ResolveIndirectArgs = ResolveIndirectArgs;
			for (uint32 IndexInBatch = 0; IndexInBatch < BatchSize; ++IndexInBatch)
			{
				const FRadianceCacheSetup& Setup = SetupOutputArray[BaseRadianceCacheIndex + IndexInBatch];
				PassParameters->RWRadianceProbeAtlasTexture[IndexInBatch] = GraphBuilder.CreateUAV(Setup.RadianceProbeAtlasTextureSource);
				PassParameters->RWDepthProbeAtlasTexture[IndexInBatch] = GraphBuilder.CreateUAV(Setup.DepthProbeAtlasTexture);
			}
			PassParameters->BatchTracingParameters = BatchTracingParameters;
			PassParameters->TraceRadianceTexture = TraceRadianceTexture;
			PassParameters->TraceHitTexture = TraceHitTexture;

			FSplatRadianceCacheIntoAtlasCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSplatRadianceCacheIntoAtlasCS::FRadianceCacheBatchSize>(BatchSize);
			auto ComputeShader = View.ShaderMap->GetShader<FSplatRadianceCacheIntoAtlasCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CompositeTracesIntoAtlas"),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				PassParameters->ResolveIndirectArgs,
				0);
		}
	}
#else
	unimplemented();
#endif // RHI_RAYTRACING
}