// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenSceneLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "DistanceFieldLightingShared.h"
#include "VolumetricCloudRendering.h"

#if RHI_RAYTRACING
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"
#endif // RHI_RAYTRACING

static TAutoConsoleVariable<int32> CVarLumenSceneDirectLightingHardwareRayTracing(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen direct lighting (Default = 1)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int> CVarLumenSceneDirectLightingHardwareRayTracingHeightfieldProjectionBias(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing.HeightfieldProjectionBias"),
	0,
	TEXT("Applies a projection bias such that an occlusion ray starts on the ray-tracing heightfield representation.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenSceneDirectLightingHardwareRayTracingHeightfieldProjectionBiasSearchRadius(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing.HeightfieldProjectionBiasSearchRadius"),
	256,
	TEXT("Determines the search radius for heightfield projection bias. Larger search radius corresponds to increased traversal cost (default = 256).\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace Lumen
{
	bool UseHardwareRayTracedDirectLighting(const FSceneViewFamily& ViewFamily)
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing(ViewFamily)
			&& (CVarLumenSceneDirectLightingHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}
} // namespace Lumen

#if RHI_RAYTRACING

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneDebugHardwareRayTracing : public FLumenHardwareRayTracingShaderBase
{
	DECLARE_LUMEN_RAYTRACING_SHADER(FLumenSceneDebugHardwareRayTracing)

	using FPermutationDomain = TShaderPermutationDomain<FLumenHardwareRayTracingShaderBase::FBasePermutationDomain>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingShaderBase::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER(float, ResolutionScale)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWDebugData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform)
			&& FLumenHardwareRayTracingShaderBase::ShouldCompilePermutation(Parameters, ShaderDispatchType);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingShaderBase::ModifyCompilationEnvironment(Parameters, ShaderDispatchType, Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_DEBUG_SCENE"), 1);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::LumenMinimal;
	}
};

IMPLEMENT_LUMEN_RAYGEN_AND_COMPUTE_RAYTRACING_SHADERS(FLumenSceneDebugHardwareRayTracing)

IMPLEMENT_GLOBAL_SHADER(FLumenSceneDebugHardwareRayTracingCS, "/Engine/Private/Lumen/LumenSceneDebugHardwareRayTracing.usf", "LumenSceneDebugHardwareRayTracingCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenSceneDebugHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenSceneDebugHardwareRayTracing.usf", "LumenSceneDebugHardwareRayTracingRGS", SF_RayGen);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenDirectLightingHardwareRayTracing : public FLumenHardwareRayTracingShaderBase
{
	DECLARE_LUMEN_RAYTRACING_SHADER(FLumenDirectLightingHardwareRayTracing)

	class FEnableFarFieldTracing : SHADER_PERMUTATION_BOOL("ENABLE_FAR_FIELD_TRACING");
	class FEnableHeightfieldProjectionBias : SHADER_PERMUTATION_BOOL("ENABLE_HEIGHTFIELD_PROJECTION_BIAS");
	class FSurfaceCacheAlphaMasking : SHADER_PERMUTATION_BOOL("SURFACE_CACHE_ALPHA_MASKING");
	class FStochastic : SHADER_PERMUTATION_BOOL("USE_STOCHASTIC");
	using FPermutationDomain = TShaderPermutationDomain<FLumenHardwareRayTracingShaderBase::FBasePermutationDomain, FEnableFarFieldTracing, FEnableHeightfieldProjectionBias, FSurfaceCacheAlphaMasking, FStochastic>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingShaderBase::FSharedParameters, SharedParameters)
		RDG_BUFFER_ACCESS(HardwareRayTracingIndirectArgs, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LightTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, LightTiles)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenSceneDirectLighting::FLightDataParameters, LumenLightData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ShadowTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ShadowTraces)

		// Constants
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(uint32, ViewIndex)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)

		SHADER_PARAMETER(float, HardwareRayTracingShadowRayBias)
		SHADER_PARAMETER(float, HeightfieldShadowReceiverBias)
		SHADER_PARAMETER(float, HeightfieldProjectionBiasSearchRadius)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWShadowMaskTiles)

		// Stochastic lighting
		SHADER_PARAMETER(FVector2f, ViewExposure)
		SHADER_PARAMETER_ARRAY(FMatrix44f, FrustumTranslatedWorldToClip, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationHigh, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationLow, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, RWLightSamples)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CompactedLightSampleData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CompactedLightSampleAllocator)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LumenSceneData)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform)
			&& FLumenHardwareRayTracingShaderBase::ShouldCompilePermutation(Parameters, ShaderDispatchType);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, Lumen::ERayTracingShaderDispatchType ShaderDispatchType, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingShaderBase::ModifyCompilationEnvironment(Parameters, ShaderDispatchType, Lumen::ESurfaceCacheSampling::AlwaysResidentPagesWithoutFeedback, OutEnvironment);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::LumenMinimal;
	}
};

IMPLEMENT_LUMEN_RAYGEN_AND_COMPUTE_RAYTRACING_SHADERS(FLumenDirectLightingHardwareRayTracing)

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenSceneDirectLightingHardwareRayTracingCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingRGS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenSceneDirectLightingHardwareRayTracingRGS", SF_RayGen);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenDirectLightingHardwareRayTracingIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenDirectLightingHardwareRayTracingIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DispatchLightTilesIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWHardwareRayTracingIndirectArgs)
		SHADER_PARAMETER(FIntPoint, OutputThreadGroupSize)
		SHADER_PARAMETER(uint32, bStochastic)
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

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingIndirectArgsCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenDirectLightingHardwareRayTracingIndirectArgsCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

float GetHeightfieldProjectionBiasSearchRadius()
{
	return FMath::Max(CVarLumenSceneDirectLightingHardwareRayTracingHeightfieldProjectionBiasSearchRadius.GetValueOnRenderThread(), 0);
}

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedDirectLighting(*View.Family) && !Lumen::UseHardwareInlineRayTracing(*View.Family))
	{
		{
			FLumenDirectLightingHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FEnableFarFieldTracing>(Lumen::UseFarField(*View.Family));
			PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FEnableHeightfieldProjectionBias>(CVarLumenSceneDirectLightingHardwareRayTracingHeightfieldProjectionBias.GetValueOnRenderThread() != 0);
			PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
			PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FStochastic>(LumenSceneDirectLighting::UseStochasticLighting(*View.Family));
			PermutationVector = FLumenDirectLightingHardwareRayTracingRGS::RemapPermutation(PermutationVector);
			TShaderRef<FLumenDirectLightingHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
		{
			FLumenSceneDebugHardwareRayTracingRGS::FPermutationDomain PermutationVector;
			TShaderRef<FLumenSceneDebugHardwareRayTracingRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenSceneDebugHardwareRayTracingRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
		}
	}
}

void SetLumenHardwareRayTracedDirectLightingShadowsParameters(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	int32 ViewIndex,
	const FLumenCardTracingParameters& TracingParameters,
	FRDGBufferRef LightTileAllocator,
	FRDGBufferRef LightTiles,
	const LumenSceneDirectLighting::FLightDataParameters& LumenLightData,
	FRDGBufferUAVRef ShadowMaskTilesUAV,
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer,
	FLumenDirectLightingHardwareRayTracingRGS::FParameters* Parameters
)
{
	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		GetSceneTextureParameters(GraphBuilder, View),
		View,
		TracingParameters,
		&Parameters->SharedParameters
	);

	Parameters->HardwareRayTracingIndirectArgs = HardwareRayTracingIndirectArgsBuffer;
	Parameters->LightTileAllocator = LightTileAllocator ? GraphBuilder.CreateSRV(LightTileAllocator) : nullptr;
	Parameters->LightTiles = LightTiles ? GraphBuilder.CreateSRV(LightTiles) : nullptr;
	Parameters->LumenLightData = LumenLightData;

	Parameters->PullbackBias = 0.0f;
	Parameters->ViewIndex = ViewIndex;
	Parameters->MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
	Parameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
	Parameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();
	
	Parameters->HardwareRayTracingShadowRayBias = LumenSceneDirectLighting::GetHardwareRayTracingShadowRayBias();
	Parameters->HeightfieldShadowReceiverBias = Lumen::GetHeightfieldReceiverBias();
	Parameters->HeightfieldProjectionBiasSearchRadius = GetHeightfieldProjectionBiasSearchRadius();

	// Output
	Parameters->RWShadowMaskTiles = ShadowMaskTilesUAV;

	// Fallback for (unused) resources
	if (!Parameters->LightTileAllocator || !Parameters->LightTiles)
	{
		FRDGBufferRef DefaultStructuredBuffer = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 16, FUintVector4::ZeroValue);
		FRDGBufferSRVRef DummySRV = GraphBuilder.CreateSRV(DefaultStructuredBuffer);

		if (Parameters->LightTileAllocator) 	{ Parameters->LightTileAllocator = DummySRV; }
		if (Parameters->LightTiles) 			{ Parameters->LightTiles = DummySRV; }
	}

	if (!Parameters->RWShadowMaskTiles)
	{
		FRDGBufferRef DummyOutputBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(16u, 16u), TEXT("Lumen.SceneLighting.DummyUAV"));
		Parameters->RWShadowMaskTiles = GraphBuilder.CreateUAV(DummyOutputBuffer);
	}

}

#endif // RHI_RAYTRACING

void TraceLumenHardwareRayTracedDirectLightingShadows(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	int32 ViewIndex,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	const FLumenDirectLightingStochasticData& StochasticData,
	const LumenSceneDirectLighting::FLightDataParameters& LumenLightData,
	FRDGBufferRef ShadowTraceIndirectArgs,
	FRDGBufferRef ShadowTraceAllocator,
	FRDGBufferRef ShadowTraces,
	FRDGBufferRef LightTileAllocator,
	FRDGBufferRef LightTiles,
	FRDGBufferUAVRef ShadowMaskTilesUAV,
	ERDGPassFlags ComputePassFlags)
{
#if RHI_RAYTRACING
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing(*View.Family);
	const bool bUseMinimalPayload = true;
	const bool bStochastic = StochasticData.IsValid();

	checkf(ComputePassFlags != ERDGPassFlags::AsyncCompute || bInlineRayTracing, TEXT("Async Lumen HWRT is only supported for inline ray tracing"));

	// Set indirect dispatch arguments
	FRDGBufferRef HardwareRayTracingIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.Reflection.CompactTracingIndirectArgs"));
	{
		FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingIndirectArgsCS::FParameters>();
		{
			PassParameters->DispatchLightTilesIndirectArgs = GraphBuilder.CreateSRV(bStochastic ? StochasticData.CompactedLightSampleAllocator : ShadowTraceIndirectArgs, PF_R32_UINT);
			PassParameters->RWHardwareRayTracingIndirectArgs = GraphBuilder.CreateUAV(HardwareRayTracingIndirectArgsBuffer, PF_R32_UINT);
			PassParameters->OutputThreadGroupSize = bInlineRayTracing ? FLumenDirectLightingHardwareRayTracingCS::GetThreadGroupSize(View.GetShaderPlatform()) : FLumenDirectLightingHardwareRayTracingRGS::GetThreadGroupSize();
			PassParameters->bStochastic = bStochastic ? 1u : 0u;
		}

		TShaderRef<FLumenDirectLightingHardwareRayTracingIndirectArgsCS> ComputeShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingIndirectArgsCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FLumenDirectLightingHardwareRayTracingIndirectArgsCS"),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	FLumenCardTracingParameters TracingParameters;
	GetLumenCardTracingParameters(GraphBuilder, View, *Scene->GetLumenSceneData(View), FrameTemporaries, /*bSurfaceCacheFeedback*/ false, TracingParameters);

	FLumenDirectLightingHardwareRayTracing::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracing::FParameters>();
	SetLumenHardwareRayTracedDirectLightingShadowsParameters(
		GraphBuilder,
		View,
		ViewIndex,
		TracingParameters,
		LightTileAllocator,
		LightTiles,
		LumenLightData,
		ShadowMaskTilesUAV,
		HardwareRayTracingIndirectArgsBuffer,
		PassParameters
	);

	FRDGBufferSRVRef DummySRV = nullptr;
	if (!ShadowTraceAllocator || !ShadowTraces)
	{
		FRDGBufferRef DefaultStructuredBuffer = GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 16, FUintVector4::ZeroValue);
		DummySRV = GraphBuilder.CreateSRV(DefaultStructuredBuffer);
	}
	PassParameters->ShadowTraceAllocator = ShadowTraceAllocator ? GraphBuilder.CreateSRV(ShadowTraceAllocator) : DummySRV;
	PassParameters->ShadowTraces = ShadowTraces ? GraphBuilder.CreateSRV(ShadowTraces) : DummySRV;

	if (bStochastic)
	{
		check(StochasticData.LightSamples);

		const int32 NumViewOrigins = FrameTemporaries.ViewOrigins.Num();
		for (int32 OriginIndex = 0; OriginIndex < NumViewOrigins; ++OriginIndex)
		{
			const FLumenViewOrigin& ViewOrigin = FrameTemporaries.ViewOrigins[OriginIndex];

			PassParameters->FrustumTranslatedWorldToClip[OriginIndex] = ViewOrigin.FrustumTranslatedWorldToClip;
			PassParameters->PreViewTranslationHigh[OriginIndex] = ViewOrigin.PreViewTranslationDF.High;
			PassParameters->PreViewTranslationLow[OriginIndex] = ViewOrigin.PreViewTranslationDF.Low;
			PassParameters->ViewExposure[OriginIndex] = ViewOrigin.LastEyeAdaptationExposure;
		}

		PassParameters->CompactedLightSampleData = GraphBuilder.CreateSRV(StochasticData.CompactedLightSampleData);
		PassParameters->CompactedLightSampleAllocator = GraphBuilder.CreateSRV(StochasticData.CompactedLightSampleAllocator);
		PassParameters->RWLightSamples = GraphBuilder.CreateUAV(StochasticData.LightSamples);
		PassParameters->LumenSceneData = StochasticData.SceneDataTexture;
	}

	FLumenDirectLightingHardwareRayTracingRGS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FEnableFarFieldTracing>(Lumen::UseFarField(*View.Family));
	PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FEnableHeightfieldProjectionBias>(CVarLumenSceneDirectLightingHardwareRayTracingHeightfieldProjectionBias.GetValueOnRenderThread() != 0);
	PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FSurfaceCacheAlphaMasking>(LumenHardwareRayTracing::UseSurfaceCacheAlphaMasking());
	PermutationVector.Set<FLumenDirectLightingHardwareRayTracingRGS::FStochastic>(bStochastic);
	PermutationVector = FLumenDirectLightingHardwareRayTracingRGS::RemapPermutation(PermutationVector);

	if (bInlineRayTracing)
	{
		FLumenDirectLightingHardwareRayTracingCS::AddLumenRayTracingDispatchIndirect(
			GraphBuilder, 
			RDG_EVENT_NAME("LumenDirectLightingHardwareRayTracingCS"),
			View,
			PermutationVector,
			PassParameters,
			PassParameters->HardwareRayTracingIndirectArgs,
			0,
			ComputePassFlags);
	}
	else
	{
		FLumenDirectLightingHardwareRayTracingRGS::AddLumenRayTracingDispatchIndirect(
			GraphBuilder, 
			RDG_EVENT_NAME("LumenDirectLightingHardwareRayTracingRGS"),
			View, 
			PermutationVector, 
			PassParameters, 
			PassParameters->HardwareRayTracingIndirectArgs, 
			0, 
			bUseMinimalPayload);
	}
#else
	unimplemented();
#endif // RHI_RAYTRACING
}

FRDGBufferSRVRef TraceLumenHardwareRayTracedDebug(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	int32 ViewIndex,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	ERDGPassFlags ComputePassFlags)
{
	const bool bUseHardwareRayTracing = Lumen::UseHardwareRayTracedDirectLighting(*View.Family);
	if (!bUseHardwareRayTracing)
	{
		return nullptr;
	}

	FRDGBufferRef OutDebugBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4u, 16u), TEXT("LumenScene.DebugData"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutDebugBuffer), 0u);

	#if RHI_RAYTRACING
	const bool bInlineRayTracing = Lumen::UseHardwareInlineRayTracing(*View.Family);
	const bool bUseMinimalPayload = true;
	checkf(ComputePassFlags != ERDGPassFlags::AsyncCompute || bInlineRayTracing, TEXT("Async Lumen HWRT is only supported for inline ray tracing"));

	FLumenCardTracingParameters TracingParameters;
	GetLumenCardTracingParameters(GraphBuilder, View, *Scene->GetLumenSceneData(View), FrameTemporaries, /*bSurfaceCacheFeedback*/ false, TracingParameters);
	FLumenSceneDebugHardwareRayTracing::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneDebugHardwareRayTracing::FParameters>();
	SetLumenHardwareRayTracingSharedParameters(
		GraphBuilder,
		GetSceneTextureParameters(GraphBuilder, View),
		View,
		TracingParameters,
		&PassParameters->SharedParameters);
	ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintUniformBuffer);
	PassParameters->ResolutionScale = float(View.ViewRect.Width()) / float(View.UnscaledViewRect.Width());
	PassParameters->RWDebugData = GraphBuilder.CreateUAV(OutDebugBuffer);

	FLumenSceneDebugHardwareRayTracing::FPermutationDomain PermutationVector;
	if (bInlineRayTracing)
	{
		FLumenSceneDebugHardwareRayTracingCS::AddLumenRayTracingDispatch(
			GraphBuilder, 
			RDG_EVENT_NAME("LumenSceneDebugHardwareRayTracingCS"),
			View,
			PermutationVector,
			PassParameters,
			FIntVector(1,1,1),
			ComputePassFlags);
	}
	else
	{
		FLumenSceneDebugHardwareRayTracingRGS::AddLumenRayTracingDispatch(
			GraphBuilder, 
			RDG_EVENT_NAME("LumenSceneDebugHardwareRayTracingRGS"),
			View, 
			PermutationVector, 
			PassParameters, 
			FIntPoint(1,1), 
			bUseMinimalPayload);
	}
	#else
	unimplemented();
	#endif // RHI_RAYTRACING
	return GraphBuilder.CreateSRV(OutDebugBuffer);
}