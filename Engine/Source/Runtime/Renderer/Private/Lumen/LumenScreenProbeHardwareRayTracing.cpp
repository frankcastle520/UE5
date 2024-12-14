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
#include "LumenReflections.h"
#include "LumenRadianceCache.h"
#include "LumenScreenProbeGather.h"
#include "LumenHardwareRayTracingCommon.h"

#if RHI_RAYTRACING
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#endif // RHI_RAYTRACING

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracing(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing"),
	1,
	TEXT("0. Software raytracing of diffuse indirect from Lumen cubemap tree.")
	TEXT("1. Enable hardware ray tracing of diffuse indirect. (Default)\n"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingNormalBias(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.NormalBias"),
	.1f,
	TEXT("Bias along the shading normal, useful when the Ray Tracing geometry doesn't match the GBuffer (Nanite Proxy geometry)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenScreenProbeGatherHardwareRayTracingRetraceFarField(
	TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing.Retrace.FarField"),
	1,
	TEXT("Determines whether a second trace will be fired for far-field contribution (Default = 1)"),
	ECVF_RenderThreadSafe
);

namespace Lumen
{
	bool UseHardwareRayTracedScreenProbeGather(const FSceneViewFamily& ViewFamily)
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing(ViewFamily)
			&& (CVarLumenScreenProbeGatherHardwareRayTracing.GetValueOnAnyThread() != 0);
#else
		return false;
#endif
	}
}

namespace LumenScreenProbeGather
{
	bool UseFarField(const FSceneViewFamily& ViewFamily)
	{
#if RHI_RAYTRACING
		return Lumen::UseFarField(ViewFamily) && CVarLumenScreenProbeGatherHardwareRayTracingRetraceFarField.GetValueOnRenderThread();
#else
		return false;
#endif
	}

	enum class ERayTracingPass
	{
		Default,
		FarField,
		HitLighting,
		MAX
	};
}

bool LumenScreenProbeGather::UseHitLighting(const FViewInfo& View, EDiffuseIndirectMethod DiffuseIndirectMethod)
{
	if (LumenHardwareRayTracing::IsRayGenSupported())
	{
		return LumenHardwareRayTracing::GetHitLightingMode(View, DiffuseIndirectMethod) == LumenHardwareRayTracing::EHitLightingMode::HitLighting;
	}

	return false;
}

#if RHI_RAYTRACING

class FLumenScreenProbeGatherHardwareRayTracing : public FLumenHardwareRayTracingShaderBase
{
	DECLARE_LUMEN_RAYTRACING_SHADER(FLumenScreenProbeGatherHardwareRayTracing)

	class FRayTracingPass : SHADER_PERMUTATION_ENUM_CLASS("RAY_TRACING_PASS", LumenScreenProbeGather::ERayTracingPass);
	class FAvoidSelfIntersectionsMode : SHADER_PERMUTATION_ENUM_CLASS("AVOID_SELF_INTERSECTIONS_MODE", LumenHardwareRayTracing::EAvoidSelfIntersectionsMode);
	class FRadianceCache : SHADER_PERMUTATION_BOOL("DIM_RADIANCE_CACHE");
	class FStructuredImportanceSamplingDim : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	class FSurfaceCacheAlphaMasking : SHADER_PERMUTATION_BOOL("SURFACE_CACHE_ALPHA_MASKING");

	using FPermutationDomain = TShaderPermutationDomain<FLumenHardwareRayTracingShaderBase::FBasePermutationDomain, FAvoidSelfIntersectionsMode, FRayTracingPass, FRadianceCache, FStructuredImportanceSamplingDim, FSurfaceCacheAlphaMasking>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingShaderBase::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)

		// Screen probes
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)

		// Constants
		SHADER_PARAMETER(uint32, HitLightingShadowMode)
		SHADER_PARAMETER(uint32, HitLightingDirectLighting)
		SHADER_PARAMETER(uint32, HitLightingSkylight)
		SHADER_PARAMETER(float, NearFieldMaxTraceDistance)
		SHADER_PARAMETER(float, NearFieldMaxTraceDistanceDitherScale)
		SHADER_PARAMETER(float, NearFieldSceneRadius)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(float, NormalBias)
		SHADER_PARAMETER(float, FarFieldBias)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FRayTracingPass>() == LumenScreenProbeGather::ERayTracingPass::FarField)
		{
			PermutationVector.Set<FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::EAvoidSelfIntersectionsMode::Disabled);
			PermutationVector.Set<FRadianceCache>(false);
			PermutationVector.Set<FSurfaceCacheAlphaMasking>(false);
		}
		else if (PermutationVector.Get<FRayTracingPass>() == LumenScreenProbeGather::ERayTracingPass::HitLighting)
		{
			PermutationVector.Set<FSurfaceCacheAlphaMasking>(false);

			// Lumen global AHS can't be supported with Hit Lighting as AHS is used for material alpha masking
			if (PermutationVector.Get<FAvoidSelfIntersectionsMode>() == LumenHardwareRayTracing::EAvoidSelfIntersectionsMode::AHS)
			{
				PermutationVector.Set<FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::EAvoidSelfIntersectionsMode::Retrace);
			}
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

		if (ShaderDispatchType == Lumen::ERayTracingShaderDispatchType::Inline && PermutationVector.Get<FRayTracingPass>() == LumenScreenProbeGather::ERayTracingPass::HitLighting)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform)
			&& FLumenHardwareRayTracingShaderBase::ShouldCompilePermutation(Parameters, ShaderDispatchType);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingShaderBase::ModifyCompilationEnvironment(Parameters, ShaderDispatchType, Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FRayTracingPass>() == LumenScreenProbeGather::ERayTracingPass::Default)
		{
			OutEnvironment.SetDefine(TEXT("ENABLE_NEAR_FIELD_TRACING"), 1);
		}

		if (PermutationVector.Get<FRayTracingPass>() == LumenScreenProbeGather::ERayTracingPass::FarField)
		{
			OutEnvironment.SetDefine(TEXT("ENABLE_FAR_FIELD_TRACING"), 1);
		}
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		FPermutationDomain PermutationVector(PermutationId);
		if (PermutationVector.Get<FRayTracingPass>() == LumenScreenProbeGather::ERayTracingPass::HitLighting)
		{
			return ERayTracingPayloadType::RayTracingMaterial;
		}
		else
		{
			return ERayTracingPayloadType::LumenMinimal;
		}
	}
};

IMPLEMENT_LUMEN_RAYGEN_AND_COMPUTE_RAYTRACING_SHADERS(FLumenScreenProbeGatherHardwareRayTracing)

IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingCS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenScreenProbeGatherHardwareRayTracingCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "LumenScreenProbeGatherHardwareRayTracingRGS", SF_RayGen);

class FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
		SHADER_PARAMETER(FIntPoint, OutputThreadGroupSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_GLOBAL_SHADER(FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf", "FLumenScreenProbeHardwareRayTracingIndirectArgsCS", SF_Compute);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingScreenProbeGather(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedScreenProbeGather(*View.Family) && LumenScreenProbeGather::UseHitLighting(View, GetViewPipelineState(View).DiffuseIndirectMethod))
	{
		FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRayTracingPass>(LumenScreenProbeGather::ERayTracingPass::HitLighting);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::GetAvoidSelfIntersectionsMode());
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(LumenScreenProbeGather::UseRadianceCache());
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
		PermutationVector = FLumenScreenProbeGatherHardwareRayTracingRGS::RemapPermutation(PermutationVector);

		TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);
		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingScreenProbeGatherLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedScreenProbeGather(*View.Family) && !Lumen::UseHardwareInlineRayTracing(*View.Family))
	{
		const bool bUseRadianceCache = LumenScreenProbeGather::UseRadianceCache();
		const bool bUseFarField = LumenScreenProbeGather::UseFarField(*View.Family);

		// Default trace
		{
			FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRayTracingPass>(LumenScreenProbeGather::ERayTracingPass::Default);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::GetAvoidSelfIntersectionsMode());
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(bUseRadianceCache);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
			PermutationVector = FLumenScreenProbeGatherHardwareRayTracingRGS::RemapPermutation(PermutationVector);

			TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}

		// Far-field trace
		if (bUseFarField)
		{
			FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRayTracingPass>(LumenScreenProbeGather::ERayTracingPass::FarField);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::GetAvoidSelfIntersectionsMode());
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FRadianceCache>(false);
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FStructuredImportanceSamplingDim>(LumenScreenProbeGather::UseImportanceSampling(View));
			PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
			PermutationVector = FLumenScreenProbeGatherHardwareRayTracingRGS::RemapPermutation(PermutationVector);

			TShaderRef<FLumenScreenProbeGatherHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingRGS>(PermutationVector);

			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void DispatchLumenScreenProbeGatherHardwareRayTracingIndirectArgs(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGBufferRef HardwareRayTracingIndirectArgsBuffer, const FCompactedTraceParameters& CompactedTraceParameters, FIntPoint OutputThreadGroupSize, ERDGPassFlags ComputePassFlags)
{
	FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS::FParameters>();

	PassParameters->CompactedTraceTexelAllocator = CompactedTraceParameters.CompactedTraceTexelAllocator;
	PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
	PassParameters->OutputThreadGroupSize = OutputThreadGroupSize;

	TShaderRef<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenScreenProbeGatherHardwareRayTracingIndirectArgsCS>();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("LumenScreenProbeGatherHardwareRayTracingIndirectArgsCS"),
		ComputePassFlags,
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1));
}

void DispatchRayGenOrComputeShader(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	FScreenProbeParameters& ScreenProbeParameters,
	const FLumenCardTracingParameters& TracingParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const FCompactedTraceParameters& CompactedTraceParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FLumenScreenProbeGatherHardwareRayTracingRGS::FPermutationDomain& PermutationVector,
	EDiffuseIndirectMethod DiffuseIndirectMethod,
	bool bInlineRayTracing,
	ERDGPassFlags ComputePassFlags)
{
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.ScreenProbeGather.HardwareRayTracing.IndirectArgsCS"));
	FIntPoint OutputThreadGroupSize = bInlineRayTracing ? FLumenScreenProbeGatherHardwareRayTracingCS::GetThreadGroupSize(View.GetShaderPlatform()) : FLumenScreenProbeGatherHardwareRayTracingRGS::GetThreadGroupSize();
	DispatchLumenScreenProbeGatherHardwareRayTracingIndirectArgs(GraphBuilder, View, HardwareRayTracingIndirectArgsBuffer, CompactedTraceParameters, OutputThreadGroupSize, ComputePassFlags);

	FLumenScreenProbeGatherHardwareRayTracing::FParameters* Parameters = GraphBuilder.AllocParameters<FLumenScreenProbeGatherHardwareRayTracing::FParameters>();
	{
		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			SceneTextures,
			View,
			TracingParameters,
			&Parameters->SharedParameters
		);

		Parameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
		Parameters->IndirectTracingParameters = IndirectTracingParameters;
		Parameters->ScreenProbeParameters = ScreenProbeParameters;
		Parameters->RadianceCacheParameters = RadianceCacheParameters;
		Parameters->CompactedTraceParameters = CompactedTraceParameters;

		const bool bUseFarField = LumenScreenProbeGather::UseFarField(*View.Family);
		const float NearFieldMaxTraceDistance = Lumen::GetMaxTraceDistance(View);
		
		Parameters->HitLightingShadowMode = LumenHardwareRayTracing::GetHitLightingShadowMode();
		Parameters->HitLightingDirectLighting = LumenHardwareRayTracing::UseHitLightingDirectLighting() ? 1 : 0;
		Parameters->HitLightingSkylight = LumenHardwareRayTracing::UseHitLightingSkylight(DiffuseIndirectMethod) ? 1 : 0;
		Parameters->NearFieldMaxTraceDistance = NearFieldMaxTraceDistance;
		Parameters->FarFieldMaxTraceDistance = bUseFarField ? Lumen::GetFarFieldMaxTraceDistance() : NearFieldMaxTraceDistance;
		Parameters->NearFieldMaxTraceDistanceDitherScale = Lumen::GetNearFieldMaxTraceDistanceDitherScale(bUseFarField);
		Parameters->NearFieldSceneRadius = Lumen::GetNearFieldSceneRadius(View, bUseFarField);	
		Parameters->FarFieldBias = LumenHardwareRayTracing::GetFarFieldBias();
		Parameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
		Parameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
		Parameters->NormalBias = CVarLumenHardwareRayTracingNormalBias.GetValueOnRenderThread();
	}

	const LumenScreenProbeGather::ERayTracingPass RayTracingPass = PermutationVector.Get<FLumenScreenProbeGatherHardwareRayTracing::FRayTracingPass>();
	const FString RayTracingPassName = RayTracingPass == LumenScreenProbeGather::ERayTracingPass::HitLighting ? TEXT("hit-lighting") : (RayTracingPass == LumenScreenProbeGather::ERayTracingPass::FarField ? TEXT("far-field") : TEXT("default"));

	const bool bUseMinimalPayload = RayTracingPass != LumenScreenProbeGather::ERayTracingPass::HitLighting;
	if (bInlineRayTracing && bUseMinimalPayload)
	{
		FLumenScreenProbeGatherHardwareRayTracingCS::AddLumenRayTracingDispatchIndirect(
			GraphBuilder,
			RDG_EVENT_NAME("HardwareRayTracingCS %s", *RayTracingPassName),
			View,
			PermutationVector,
			Parameters,
			Parameters->HardwareRayTracingIndirectArgs,
			0,
			ComputePassFlags);
	}
	else
	{
		FLumenScreenProbeGatherHardwareRayTracingRGS::AddLumenRayTracingDispatchIndirect(
			GraphBuilder,
			RDG_EVENT_NAME("HardwareRayTracingRGS %s", *RayTracingPassName),
			View,
			PermutationVector,
			Parameters,
			Parameters->HardwareRayTracingIndirectArgs,
			0,
			bUseMinimalPayload);
	}	
}

#endif // RHI_RAYTRACING

void RenderHardwareRayTracingScreenProbe(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& ScreenProbeParameters,
	const FViewInfo& View,
	const FLumenCardTracingParameters& TracingParameters,
	FLumenIndirectTracingParameters& IndirectTracingParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	bool bRenderDirectLighting,
	ERDGPassFlags ComputePassFlags)
#if RHI_RAYTRACING
{
	// Async only supported for inline ray tracing
	check(ComputePassFlags != ERDGPassFlags::AsyncCompute || Lumen::UseHardwareInlineRayTracing(*View.Family));

	const uint32 NumTracesPerProbe = ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	FIntPoint RayTracingResolution = FIntPoint(ScreenProbeParameters.ScreenProbeAtlasViewSize.X * ScreenProbeParameters.ScreenProbeAtlasViewSize.Y * NumTracesPerProbe, 1);
	int32 MaxRayCount = RayTracingResolution.X * RayTracingResolution.Y;

	const EDiffuseIndirectMethod DiffuseIndirectMethod = EDiffuseIndirectMethod::Lumen;
	const bool bFarField = LumenScreenProbeGather::UseFarField(*View.Family);
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing(*View.Family);
	const bool bUseRadianceCache = LumenScreenProbeGather::UseRadianceCache();
	const bool bUseImportanceSampling = LumenScreenProbeGather::UseImportanceSampling(View);
	const bool bUseHitLighting = LumenScreenProbeGather::UseHitLighting(View, DiffuseIndirectMethod);

	// Default tracing for near field
	{
		FCompactedTraceParameters CompactedTraceParameters = LumenScreenProbeGather::CompactTraces(
			GraphBuilder,
			View,
			ScreenProbeParameters,
			false,
			0.0f,
			IndirectTracingParameters.MaxTraceDistance,
			bRenderDirectLighting,
			/*bCompactForFarField*/ false,
			ComputePassFlags);

		FLumenScreenProbeGatherHardwareRayTracing::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FRayTracingPass>(bUseHitLighting ? LumenScreenProbeGather::ERayTracingPass::HitLighting : LumenScreenProbeGather::ERayTracingPass::Default);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::GetAvoidSelfIntersectionsMode());
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FRadianceCache>(bUseRadianceCache);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FStructuredImportanceSamplingDim>(bUseImportanceSampling);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
		PermutationVector = FLumenScreenProbeGatherHardwareRayTracing::RemapPermutation(PermutationVector);

		DispatchRayGenOrComputeShader(GraphBuilder, Scene, SceneTextures, View, ScreenProbeParameters, TracingParameters, IndirectTracingParameters,
			CompactedTraceParameters, RadianceCacheParameters, PermutationVector, DiffuseIndirectMethod, bInlineRayTracing, ComputePassFlags);
	}

	if (bFarField)
	{
		FCompactedTraceParameters CompactedTraceParameters = LumenScreenProbeGather::CompactTraces(
			GraphBuilder,
			View,
			ScreenProbeParameters,
			false,
			0.0f,
			Lumen::GetFarFieldMaxTraceDistance(),
			bRenderDirectLighting,
			/*bCompactForFarField*/ true,
			ComputePassFlags);

		FLumenScreenProbeGatherHardwareRayTracing::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FRayTracingPass>(LumenScreenProbeGather::ERayTracingPass::FarField);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FAvoidSelfIntersectionsMode>(LumenHardwareRayTracing::GetAvoidSelfIntersectionsMode());
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FRadianceCache>(false);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FStructuredImportanceSamplingDim>(bUseImportanceSampling);
		PermutationVector.Set<FLumenScreenProbeGatherHardwareRayTracing::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
		PermutationVector = FLumenScreenProbeGatherHardwareRayTracing::RemapPermutation(PermutationVector);

		DispatchRayGenOrComputeShader(GraphBuilder, Scene, SceneTextures, View, ScreenProbeParameters, TracingParameters, IndirectTracingParameters,
			CompactedTraceParameters, RadianceCacheParameters, PermutationVector, DiffuseIndirectMethod, bInlineRayTracing, ComputePassFlags);
	}
}
#else // RHI_RAYTRACING
{
	unimplemented();
}
#endif // RHI_RAYTRACING