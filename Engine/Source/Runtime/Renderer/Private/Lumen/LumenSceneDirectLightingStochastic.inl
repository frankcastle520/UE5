// Copyright Epic Games, Inc. All Rights Reserved.

static TAutoConsoleVariable<int32> CVarLumenDirectLightingStochastic(
	TEXT("r.LumenScene.DirectLighting.Stochastic"),
	0,
	TEXT("Whether to enable stochastic lighting for Lumen scene (experimental)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarLumenDirectLightingStochasticTemporal(
	TEXT("r.LumenScene.DirectLighting.Stochastic.Temporal"),
	1,
	TEXT("Enable temporal filtering."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenDirectLightingStochasticTemporalMaxFramesAccumulated(
	TEXT("r.LumenScene.DirectLighting.Stochastic.Temporal.MaxFramesAccumulated"),
	12,
	TEXT("Max history length when accumulating frames. Lower values have less ghosting, but more noise."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenDirectLightingStochasticTemporalNeighborhoodClampScale(
	TEXT("r.LumenScene.DirectLighting.Stochastic.Temporal.NeighborhoodClampScale"),
	2.0f,
	TEXT("Scales how permissive is neighborhood clamp. Higher values cause more ghosting, but allow smoother temporal accumulation."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenDirectLightingStochasticDebug(
	TEXT("r.LumenScene.DirectLighting.Stochastic.Debug"),
	0,
	TEXT("Enable debug print for Lumen stochastic pipeline."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenDirectLightingStochasticSamplePerTexel(
	TEXT("r.LumenScene.DirectLighting.Stochastic.SamplePerTexel"),
	1,
	TEXT("Number of light sample per texel for Lumen direct lighting with stochastic selection."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenDirectLightingStochasticMinWeight(
	TEXT("r.LumenScene.DirectLighting.Stochastic.MinWeight"),
	0.001f,
	TEXT("Determines minimal sample influence on final texels. Used to skip samples which would have minimal impact to the final image even if light is fully visible."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static uint32 GetLumenStochasticNumSamplesPerTexel()
{
	const int32 NumSamples = FMath::Clamp(CVarLumenDirectLightingStochasticSamplePerTexel.GetValueOnRenderThread(), 1, 4);
	return NumSamples > 2 ? 4 : NumSamples;
}

bool LumenSceneDirectLighting::UseStochasticLighting(const FSceneViewFamily& ViewFamily)
{
	return CVarLumenDirectLightingStochastic.GetValueOnRenderThread() > 0 && Lumen::UseHardwareRayTracedDirectLighting(ViewFamily);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneCompactLightOffsetCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneCompactLightOffsetCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneCompactLightOffsetCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumLights)
		SHADER_PARAMETER(uint32, NumStandaloneLights)
		SHADER_PARAMETER(uint32, NumSamplesPerPixel1d)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightCounters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTilePerLightOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCardTilePerLightArgs)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

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
		//ShaderPrint::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("SHADER_STANDALONE_COMPACT_OFFSET"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneCompactLightOffsetCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneCompactLightListCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneCompactLightListCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneCompactLightListCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, UniqueLightIndices)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, UniqueLightCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTilePerLightCounters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTilePerLightDatas)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

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
		OutEnvironment.SetDefine(TEXT("SHADER_STANDALONE_COMPACT_LIST"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneCompactLightListCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////
// Only used for light with non-atlased light function

class FLumenSceneEvaluateStandaloneLightMaterialCS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenSceneEvaluateStandaloneLightMaterialCS, Material);

	FLumenSceneEvaluateStandaloneLightMaterialCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(
			this,
			Initializer.PermutationId,
			Initializer.ParameterMap,
			*FParameters::FTypeInfo::GetStructMetadata(),
			// Don't require full bindings, we use FMaterialShader::SetParameters
			false);
	}
	FLumenSceneEvaluateStandaloneLightMaterialCS() {}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32, LightIndex)
		SHADER_PARAMETER(uint32, ViewIndex)
		SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationHigh, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationLow, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER(FVector2f, ViewExposure)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightCloudTransmittanceParameters, LightCloudTransmittanceParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenSceneDirectLighting::FLightDataParameters, LumenLightData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightCounters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightDatas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LumenSceneData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, RWLightSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWSampleDiffuseLighting)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightFunctionParameters, LightFunctionParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FCloudTransmittance : SHADER_PERMUTATION_BOOL("USE_CLOUD_TRANSMITTANCE");
	using FPermutationDomain = TShaderPermutationDomain<FCloudTransmittance>;

	static int32 GetGroupSize()
	{	
		return 8;
	}

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return Parameters.MaterialParameters.MaterialDomain == EMaterialDomain::MD_LightFunction && DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("SHADER_STANDALONE_EVALUATE"), 1);
		OutEnvironment.SetDefine(TEXT("LIGHT_FUNCTION"), 1);
		OutEnvironment.SetDefine(TEXT("USE_IES_PROFILE"), 1);	// To avoid extra permutation
		OutEnvironment.SetDefine(TEXT("USE_RECT_LIGHT"), 1); 	// To avoid extra permutation
		OutEnvironment.SetDefine(TEXT("USE_LIGHT_FUNCTION_ATLAS"), 0);
		OutEnvironment.SetDefine(TEXT("SUBSTRATE_INLINE_SHADING"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FLumenSceneEvaluateStandaloneLightMaterialCS, TEXT("/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf"), TEXT("MainCS"), SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

// Only used for direct light with cloud transmittance
class FLumenSceneEvaluateStandaloneLightCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneEvaluateStandaloneLightCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneEvaluateStandaloneLightCS, FGlobalShader)

	class FCloudTransmittance : SHADER_PERMUTATION_BOOL("USE_CLOUD_TRANSMITTANCE");
	using FPermutationDomain = TShaderPermutationDomain<FCloudTransmittance>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32, LightIndex)
		SHADER_PARAMETER(uint32, ViewIndex)
		SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationHigh, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationLow, [LUMEN_MAX_VIEWS])
		SHADER_PARAMETER(FVector2f, ViewExposure)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLightCloudTransmittanceParameters, LightCloudTransmittanceParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenSceneDirectLighting::FLightDataParameters, LumenLightData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightCounters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightOffsets)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CardTilePerLightDatas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LumenSceneData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, RWLightSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWSampleDiffuseLighting)
	END_SHADER_PARAMETER_STRUCT()

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
		OutEnvironment.SetDefine(TEXT("SHADER_STANDALONE_EVALUATE"), 1);
		OutEnvironment.SetDefine(TEXT("LIGHT_FUNCTION"), 0);
		OutEnvironment.SetDefine(TEXT("USE_IES_PROFILE"), 0);			// Directional light does not support IES profile
		OutEnvironment.SetDefine(TEXT("USE_RECT_LIGHT"), 0);			// Directional light
		OutEnvironment.SetDefine(TEXT("USE_LIGHT_FUNCTION_ATLAS"), 0); 	// Directional light does not use light function atlas
		OutEnvironment.SetDefine(TEXT("SUBSTRATE_INLINE_SHADING"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneEvaluateStandaloneLightCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "MainCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SHADER_PARAMETER_STRUCT(FLumenSceneLightingStochasticParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(LightFunctionAtlas::FLightFunctionAtlasGlobalParameters, LightFunctionAtlas)
	SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)
	SHADER_PARAMETER(uint32, NumSamplesPerPixel1d)
	SHADER_PARAMETER(uint32, StateFrameIndex)
	SHADER_PARAMETER(uint32, MaxCompositeTiles)
	SHADER_PARAMETER(float, SamplingMinWeight)
	SHADER_PARAMETER(float, TemporalMaxFramesAccumulated)
	SHADER_PARAMETER(float, TemporalNeighborhoodClampScale)
	SHADER_PARAMETER(int32, TemporalAdvanceFrame)
	SHADER_PARAMETER(int32, DebugLightId)
	SHADER_PARAMETER(uint32, DummyZeroForFixingShaderCompilerBug)
	SHADER_PARAMETER(uint32, NumLights)
	SHADER_PARAMETER(uint32, NumStandaloneLights)
	SHADER_PARAMETER(uint32, NumViews)
	SHADER_PARAMETER(float, DiffuseColorBoost)
	SHADER_PARAMETER_ARRAY(FMatrix44f, FrustumTranslatedWorldToClip, [LUMEN_MAX_VIEWS])
	SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationHigh, [LUMEN_MAX_VIEWS])
	SHADER_PARAMETER_ARRAY(FVector4f, PreViewTranslationLow, [LUMEN_MAX_VIEWS])
	SHADER_PARAMETER(FVector2f, ViewExposure)
END_SHADER_PARAMETER_STRUCT()

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneGenerateLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneGenerateLightSamplesCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneGenerateLightSamplesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenSceneDirectLighting::FLightDataParameters, LumenLightData)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenSceneLightingStochasticParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, RWSampleLuminanceSum)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, RWSampleDiffuseLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSceneData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCardTilePerLightCounters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, RWLightSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWUniqueLightIndices)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWUniqueLightCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LumenSceneDebugData)
	END_SHADER_PARAMETER_STRUCT()

	class FIESProfile : SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	class FRectLight : SHADER_PERMUTATION_BOOL("USE_RECT_LIGHT");
	class FLightFunctionAtlas : SHADER_PERMUTATION_BOOL("USE_LIGHT_FUNCTION_ATLAS");
	class FNumSamplesPerPixel1d : SHADER_PERMUTATION_SPARSE_INT("NUM_SAMPLES_PER_PIXEL_1D", 1, 2, 4);
	using FPermutationDomain = TShaderPermutationDomain<FIESProfile, FRectLight, FLightFunctionAtlas, FNumSamplesPerPixel1d>;

	static int32 GetGroupSize()
	{	
		return 8;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return EShaderPermutationPrecacheRequest::Precached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		//ShaderPrint::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("SHADER_GENERATE_SAMPLE"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneGenerateLightSamplesCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "GenerateLightSamplesCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneShadeLightSamplesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneShadeLightSamplesCS);
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneShadeLightSamplesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, DummyZeroForFixingShaderCompilerBug)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<uint>, LightSamples)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<float4>, SampleDiffuseLighting)
		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(float, DiffuseColorBoost)
		SHADER_PARAMETER(uint32, NumSamplesPerPixel1d)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectLightingAtlas)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampedSampler)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LumenSceneDebugData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWFinalLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWDirectLightingAtlas)
		SHADER_PARAMETER(FVector2f, IndirectLightingAtlasHalfTexelSize)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	class FUseLightSamples : SHADER_PERMUTATION_BOOL("USE_LIGHT_SAMPLES");
	using FPermutationDomain = TShaderPermutationDomain<FUseLightSamples>;

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
		OutEnvironment.SetDefine(TEXT("SHADER_SHADING"), 1);
	}
	
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneShadeLightSamplesCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "ShadeLightSamplesCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneCompactLightSampleTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneCompactLightSampleTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneCompactLightSampleTracesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCompactedTraceTexelData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<uint>, LightSamples)
		SHADER_PARAMETER(FIntPoint, SampleViewSize)
		SHADER_PARAMETER(uint32, NumSamplesPerPixel1d)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 16; // TODO, could we reduce that to 8, so that we can load tile directly? and dispatch indirect?
	}

	class FWaveOps : SHADER_PERMUTATION_BOOL("WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FORCENOINLINE static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("SHADER_COMPACTION"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneCompactLightSampleTracesCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "CompactLightSampleTracesCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

class FLumenSceneDenoiserTemporalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenSceneDenoiserTemporalCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenSceneDenoiserTemporalCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenSceneLightingStochasticParameters, CommonParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, SampleLuminanceSumTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DiffuseLightingAndSecondMomentHistoryTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UNORM float>, NumFramesAccumulatedHistoryTexture)
		SHADER_PARAMETER(float, PrevSceneColorPreExposureCorrection)
		SHADER_PARAMETER(FVector2f, IndirectLightingAtlasHalfTexelSize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDiffuseLightingAndSecondMoment)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UNORM float>, RWNumFramesAccumulated)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TileData)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AlbedoAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ResolvedDirectLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectLightingAtlas)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampedSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, FinalLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWFinalLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWDirectLightingAtlas)
	END_SHADER_PARAMETER_STRUCT()

	class FValidHistory : SHADER_PERMUTATION_BOOL("VALID_HISTORY");
	using FPermutationDomain = TShaderPermutationDomain<FValidHistory>;

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
		OutEnvironment.SetDefine(TEXT("SHADER_TEMPORAL_DENOISER"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenSceneDenoiserTemporalCS, "/Engine/Private/Lumen/LumenSceneDirectLightingStochastic.usf", "DenoiserTemporalCS", SF_Compute);

///////////////////////////////////////////////////////////////////////////////////////////////////

static void CompactLumenSceneLightsTraces(
	const FViewInfo& View,
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef LightSamples,
	FRDGBufferRef CompactedTraceTexelData,
	FRDGBufferRef CompactedTraceTexelAllocator)
{
	// Compact light sample traces before tracing
	FLumenSceneCompactLightSampleTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneCompactLightSampleTracesCS::FParameters>();
	PassParameters->RWCompactedTraceTexelData = GraphBuilder.CreateUAV(CompactedTraceTexelData);
	PassParameters->RWCompactedTraceTexelAllocator = GraphBuilder.CreateUAV(CompactedTraceTexelAllocator);
	PassParameters->LightSamples = LightSamples;
	PassParameters->NumSamplesPerPixel1d = LightSamples->Desc.ArraySize;
	PassParameters->SampleViewSize = LightSamples->Desc.Extent;
	ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintUniformBuffer);

	const bool bWaveOps = Lumen::UseWaveOps(View.GetShaderPlatform()) && 
		GRHIMinimumWaveSize <= 32 && 
		GRHIMaximumWaveSize >= 32;

	FLumenSceneCompactLightSampleTracesCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenSceneCompactLightSampleTracesCS::FWaveOps>(bWaveOps);
	auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneCompactLightSampleTracesCS>(PermutationVector);

	FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(LightSamples->Desc.Extent, FLumenSceneCompactLightSampleTracesCS::GetGroupSize());
	GroupCount.Z = PassParameters->NumSamplesPerPixel1d;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CompactLightSampleTraces%s", bWaveOps ? TEXT("(WaveOps)") : TEXT("")),
		ComputeShader,
		PassParameters,
		GroupCount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void ComputeStochasticLighting(
	FRDGBuilder& GraphBuilder, 
	FScene* Scene,
	const FViewInfo& View,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	const FLumenDirectLightingTaskData* LightingTaskData,
	const FLumenCardUpdateContext& CardUpdateContext,
	ERDGPassFlags ComputePassFlags,
	const LumenSceneDirectLighting::FLightDataParameters& LumenLightData)
{
	FLumenSceneData& LumenSceneData = *Scene->GetLumenSceneData(View);// TODO Views[x]?

	const int32 NumViewOrigins = FrameTemporaries.ViewOrigins.Num();
	const bool bDebug = CVarLumenDirectLightingStochasticDebug.GetValueOnRenderThread() > 0;
	const uint32 NumSamplesPerPixel1d = GetLumenStochasticNumSamplesPerTexel();
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const bool bTemporal = CVarLumenDirectLightingStochasticTemporal.GetValueOnRenderThread() > 0;
	const bool bUseLightFunctionAtlas = LightFunctionAtlas::IsEnabled(View, LightFunctionAtlas::ELightFunctionAtlasSystem::Lumen);

	FBlueNoise BlueNoise = GetBlueNoiseGlobalParameters();
	TUniformBufferRef<FBlueNoise> BlueNoiseUniformBuffer = CreateUniformBufferImmediate(BlueNoise, EUniformBufferUsage::UniformBuffer_SingleDraw);
	
	// Common parameters
	FLumenSceneLightingStochasticParameters CommonParameters;
	{
		CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;
		CommonParameters.BlueNoise = BlueNoiseUniformBuffer;
		CommonParameters.NumSamplesPerPixel1d = NumSamplesPerPixel1d;
		CommonParameters.StateFrameIndex = View.ViewState ? View.ViewState->GetFrameIndex() : 0;
		CommonParameters.MaxCompositeTiles = CardUpdateContext.MaxUpdateTiles;
		CommonParameters.SamplingMinWeight = FMath::Max(CVarLumenDirectLightingStochasticMinWeight.GetValueOnRenderThread(), 0.0f);
		CommonParameters.TemporalMaxFramesAccumulated = FMath::Max(CVarLumenDirectLightingStochasticTemporalMaxFramesAccumulated.GetValueOnRenderThread(), 0.0f);
		CommonParameters.TemporalNeighborhoodClampScale = CVarLumenDirectLightingStochasticTemporalNeighborhoodClampScale.GetValueOnRenderThread();
		CommonParameters.TemporalAdvanceFrame = View.ViewState && !View.bStatePrevViewInfoIsReadOnly ? 1 : 0;
		CommonParameters.DebugLightId = INDEX_NONE;
		CommonParameters.DummyZeroForFixingShaderCompilerBug = 0;
		CommonParameters.NumLights = LightingTaskData->GatheredLights.Num();
		CommonParameters.NumStandaloneLights = LightingTaskData->StandaloneLightIndices.Num();
		CommonParameters.NumViews = NumViewOrigins;
		CommonParameters.DiffuseColorBoost = 1.0f / FMath::Max(View.FinalPostProcessSettings.LumenDiffuseColorBoost, 1.0f);
		if (bUseLightFunctionAtlas)
		{
			CommonParameters.LightFunctionAtlas = LightFunctionAtlas::BindGlobalParameters(GraphBuilder, View);
		}

		check(NumViewOrigins <= CommonParameters.FrustumTranslatedWorldToClip.Num());
		for (int32 OriginIndex = 0; OriginIndex < NumViewOrigins; ++OriginIndex)
		{
			const FLumenViewOrigin& ViewOrigin = FrameTemporaries.ViewOrigins[OriginIndex];

			CommonParameters.FrustumTranslatedWorldToClip[OriginIndex] = ViewOrigin.FrustumTranslatedWorldToClip;
			CommonParameters.PreViewTranslationHigh[OriginIndex] = ViewOrigin.PreViewTranslationDF.High;
			CommonParameters.PreViewTranslationLow[OriginIndex] = ViewOrigin.PreViewTranslationDF.Low;
			CommonParameters.ViewExposure[OriginIndex] = ViewOrigin.LastEyeAdaptationExposure;
		}

		if (true || bDebug)
		{
			ShaderPrint::SetEnabled(true);
			ShaderPrint::RequestSpaceForLines(1024);
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, CommonParameters.ShaderPrintUniformBuffer);
		}
	}

	const uint32 MaxLightTiles = CardUpdateContext.MaxUpdateTiles;
	const uint32 NumLights = LightingTaskData->GatheredLights.Num();
	const uint32 NumStandaloneLights = LightingTaskData->StandaloneLightIndices.Num();
	const uint32 NumLightsRoundedUp = FMath::RoundUpToPowerOfTwo(FMath::Max(LightingTaskData->GatheredLights.Num(), 1)) * NumViewOrigins;
	const uint32 MaxLightsPerTile = FMath::RoundUpToPowerOfTwo(FMath::Clamp(CVarLumenDirectLightingMaxLightsPerTile.GetValueOnRenderThread(), 1, 32));
	const uint32 MaxCulledCardTiles = MaxLightsPerTile * MaxLightTiles;
	const bool bHasStandaloneLight = LightingTaskData->StandaloneLightIndices.Num() > 0;


	// 0. Splice card pages into tiles
	FLumenCardTileUpdateContext CardTileUpdateContext;
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SpliceCardPageIntoToTiles");

		Lumen::SpliceCardPagesIntoTiles(GraphBuilder, GlobalShaderMap, CardUpdateContext, FrameTemporaries.LumenCardSceneUniformBuffer, CardTileUpdateContext, ComputePassFlags);
	}

	// 0. Early out if no lights
	if (NumLights == 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Shading");

		FLumenSceneShadeLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneShadeLightSamplesCS::FParameters>();
		PassParameters->DummyZeroForFixingShaderCompilerBug = 0;
		PassParameters->IndirectArgsBuffer = CardTileUpdateContext.DispatchCardTilesIndirectArgs;
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
		PassParameters->DiffuseColorBoost = 1.0f / FMath::Max(View.FinalPostProcessSettings.LumenDiffuseColorBoost, 1.0f);
		PassParameters->NumSamplesPerPixel1d = CommonParameters.NumSamplesPerPixel1d;
		PassParameters->AlbedoAtlas = FrameTemporaries.AlbedoAtlas;
		PassParameters->OpacityAtlas = FrameTemporaries.OpacityAtlas;
		PassParameters->EmissiveAtlas = FrameTemporaries.EmissiveAtlas;
		PassParameters->IndirectLightingAtlas = FrameTemporaries.IndirectLightingAtlas;
		PassParameters->BilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->RWFinalLightingAtlas = GraphBuilder.CreateUAV(FrameTemporaries.FinalLightingAtlas);
		PassParameters->RWDirectLightingAtlas = GraphBuilder.CreateUAV(FrameTemporaries.DirectLightingAtlas);
		const FIntPoint IndirectLightingAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
		PassParameters->IndirectLightingAtlasHalfTexelSize = FVector2f(0.5f / IndirectLightingAtlasSize.X, 0.5f / IndirectLightingAtlasSize.Y);
		PassParameters->TileAllocator = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTileAllocator);
		PassParameters->TileData = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTiles);
		ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintUniformBuffer);

		FLumenSceneShadeLightSamplesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenSceneShadeLightSamplesCS::FUseLightSamples>(false);
		auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneShadeLightSamplesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CombineLighting CS"),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			CardTileUpdateContext.DispatchCardTilesIndirectArgs,
			(uint32)ELumenDispatchCardTilesIndirectArgsOffset::OneGroupPerCardTile);
		return;
	}

	{
		// Transient atlas for storing position and normal to avoid loading surface cache data
		const FIntPoint AtlasTileCount  = FIntPoint(128u, FMath::DivideAndRoundUp(MaxLightTiles, 128u));
		const FIntPoint AtlasResolution = AtlasTileCount * Lumen::CardTileSize;
		check(CardUpdateContext.MaxUpdateTiles <= uint32(AtlasTileCount.X * AtlasTileCount.Y));

		// Transient texture for shading
		FRDGTextureRef LightSamples = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DArray(AtlasResolution, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, CommonParameters.NumSamplesPerPixel1d),
			TEXT("LumenScene.DirectLighting.LightSamples"));

		FRDGTextureRef SampleLuminanceSum = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(AtlasResolution, PF_R16F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("LumenScene.DirectLighting.SampleLuminanceSum"));

		FRDGTextureRef SceneAlbedo = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(AtlasResolution, PF_A2B10G10R10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("LumenScene.DirectLighting.SceneAlbedo"));

		// Each texel can select a light, so there is at max 64 unique lights per 8x8 card (i.e., == AltasResolution)
		FRDGTextureRef UniqueLightIndices = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(AtlasResolution, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("LumenScene.DirectLighting.UniqueLightIndices"));

		const FIntPoint LightCounterResolution(FMath::DivideAndRoundUp(AtlasResolution.X, int32(Lumen::CardTileSize)), FMath::DivideAndRoundUp(AtlasResolution.Y, int32(Lumen::CardTileSize)));
		FRDGTextureRef UniqueLightCount = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(LightCounterResolution, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("LumenScene.DirectLighting.UniqueLightCount"));
		
		FRDGTextureRef SampleDiffuseLighting = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2DArray(AtlasResolution, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV, CommonParameters.NumSamplesPerPixel1d),
			TEXT("LumenScene.DirectLighting.SampleDiffuseLighting"));

		// When using temporal filtering, allocate an intermediate storage for direct lighting for spatially filtering neighborhood
		FRDGTextureRef ResolvedDirectLightingAtlas = FrameTemporaries.DirectLightingAtlas; 
		if (bTemporal)
		{
			ResolvedDirectLightingAtlas = GraphBuilder.CreateTexture(FrameTemporaries.DirectLightingAtlas->Desc, TEXT("LumenScene.DirectLighting.TemporaryDirectLightingAtlas"));
		}

		// Store position, normal, and view index at the sample position to avoid the loading of the cards data during the tracing.
		FRDGTextureRef SceneData = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(AtlasResolution, PF_A32B32G32R32F, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("LumenScene.DirectLighting.SceneData"));

		FRDGBufferRef CompactedLightSampleData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), AtlasResolution.X * AtlasResolution.Y), TEXT("LumenScene.DirectLighting.CompactedLightSampleData"));
		FRDGBufferRef CompactedLightSampleAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("LumenScene.DirectLighting.CompactedLightSampleAllocator"));

		FRDGTextureUAVRef SampleLuminanceSumUAV = GraphBuilder.CreateUAV(SampleLuminanceSum, ERDGUnorderedAccessViewFlags::SkipBarrier);

		FRDGBufferRef CardTilePerLightCounters = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4u, NumLights), TEXT("LumenScene.DirectLighting.CardTilePerLightCounters"));
		FRDGBufferRef CardTilePerLightOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4u, NumLights), TEXT("LumenScene.DirectLighting.CardTilePerLightOffsets"));
		FRDGBufferRef CardTilePerLightDatas = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4u, MaxCulledCardTiles), TEXT("LumenScene.DirectLighting.CardTilePerLightDatas"));

		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedLightSampleAllocator), 0);
			if (bHasStandaloneLight)
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CardTilePerLightCounters), 0u);
			}
			AddClearUAVPass(GraphBuilder,GraphBuilder.CreateUAV(SampleLuminanceSum), 0.f);
			AddClearUAVPass(GraphBuilder,GraphBuilder.CreateUAV(LightSamples), 0u);			// Needed as trace/sample compaction is dispatch on the entire resource, and we need which samples are valid or not

			// Only for debug
			AddClearUAVPass(GraphBuilder,GraphBuilder.CreateUAV(UniqueLightIndices), 0u); // Remove - Not needed just for debugging
			AddClearUAVPass(GraphBuilder,GraphBuilder.CreateUAV(UniqueLightCount), 0u); // Remove - Not needed just for debugging
		}

		// 1.1 Sample light
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Generate Sample");

			FLumenSceneGenerateLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneGenerateLightSamplesCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;

			PassParameters->RWSampleLuminanceSum = SampleLuminanceSumUAV;
			PassParameters->RWLightSamples = GraphBuilder.CreateUAV(LightSamples);
			PassParameters->LumenLightData = LumenLightData;
			PassParameters->LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
			PassParameters->TileAllocator = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTileAllocator);
			PassParameters->TileData = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTiles);
			PassParameters->IndirectArgs = CardTileUpdateContext.DispatchCardTilesIndirectArgs;
			PassParameters->RWUniqueLightIndices = GraphBuilder.CreateUAV(UniqueLightIndices);
			PassParameters->RWUniqueLightCount = GraphBuilder.CreateUAV(UniqueLightCount);
			PassParameters->RWSampleDiffuseLighting = GraphBuilder.CreateUAV(SampleDiffuseLighting);
			PassParameters->RWSceneData = GraphBuilder.CreateUAV(SceneData);
			PassParameters->LumenSceneDebugData = FrameTemporaries.DebugData;
			PassParameters->RWCardTilePerLightCounters = GraphBuilder.CreateUAV(CardTilePerLightCounters);

			FLumenSceneGenerateLightSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenSceneGenerateLightSamplesCS::FIESProfile>(LightingTaskData->bHasIESLights);
			PermutationVector.Set<FLumenSceneGenerateLightSamplesCS::FRectLight>(LightingTaskData->bHasRectLights);
			PermutationVector.Set<FLumenSceneGenerateLightSamplesCS::FLightFunctionAtlas>(bUseLightFunctionAtlas);
			PermutationVector.Set<FLumenSceneGenerateLightSamplesCS::FNumSamplesPerPixel1d>(CommonParameters.NumSamplesPerPixel1d);
			auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneGenerateLightSamplesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("GenerateSamples(SamplesPerTexel:%d)", CommonParameters.NumSamplesPerPixel1d),
				ComputeShader,
				PassParameters,
				CardTileUpdateContext.DispatchCardTilesIndirectArgs,
				1u * sizeof(FRHIDispatchIndirectParameters)); // Dispatch 1 group per card tile
		}

		// 1.2 Evaluate lighting for standalone lights
		if (bHasStandaloneLight)
		{
			// Indirect args buffer of tiles for each standalong line + all tiles covered by standalone lights
			FRDGBufferRef CardTilePerLightArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(NumLights + 1u),TEXT("LumenScene.DirectLighting.IndirectTileListArgsBuffer"));

			// Compute offset and Build indirect arts
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Compact Offset & Args");

				FLumenSceneCompactLightOffsetCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneCompactLightOffsetCS::FParameters>();
				PassParameters->NumLights = NumLights;
				PassParameters->NumStandaloneLights = NumStandaloneLights;
				PassParameters->NumSamplesPerPixel1d = NumSamplesPerPixel1d;
				PassParameters->CardTilePerLightCounters = GraphBuilder.CreateSRV(CardTilePerLightCounters);
				PassParameters->RWCardTilePerLightOffsets = GraphBuilder.CreateUAV(CardTilePerLightOffsets);
				PassParameters->RWCardTilePerLightArgs = GraphBuilder.CreateUAV(CardTilePerLightArgs);
				FLumenSceneCompactLightOffsetCS::FPermutationDomain PermutationVector;
				auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneCompactLightOffsetCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("StandaloneLight::CompactOffset"),
					ComputeShader,
					PassParameters,
					FIntVector(1,1,1));
			}

			// Compute list of tiles
			{
				RDG_EVENT_SCOPE(GraphBuilder, "Compact List");

				FRDGBufferRef CardTilePerLightCountersForInsertion = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4u, NumLights), TEXT("LumenScene.DirectLighting.CardTilePerLightCountersForInsertion"));
				FRDGBufferUAVRef RWCardTilePerLightCountersForInsertion = GraphBuilder.CreateUAV(CardTilePerLightCountersForInsertion);
				AddClearUAVPass(GraphBuilder, RWCardTilePerLightCountersForInsertion, 0u);

				FLumenSceneCompactLightListCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneCompactLightListCS::FParameters>();
				PassParameters->IndirectArgs = CardTilePerLightArgs;
				PassParameters->CardTilePerLightOffsets = GraphBuilder.CreateSRV(CardTilePerLightOffsets);
				PassParameters->UniqueLightIndices = UniqueLightIndices;
				PassParameters->UniqueLightCount = UniqueLightCount;
				PassParameters->RWCardTilePerLightCounters = RWCardTilePerLightCountersForInsertion;
				PassParameters->RWCardTilePerLightDatas = GraphBuilder.CreateUAV(CardTilePerLightDatas);

				FLumenSceneCompactLightListCS::FPermutationDomain PermutationVector;
				auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneCompactLightListCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("StandaloneLight::CompactList"),
					ComputeShader,
					PassParameters,
					CardTilePerLightArgs,
					NumLights * sizeof(FRHIDispatchIndirectParameters));
			}

			// Evaluate light
			FRDGTextureUAVRef LightSamplesUAVSkipBarrier = GraphBuilder.CreateUAV(LightSamples, ERDGUnorderedAccessViewFlags::SkipBarrier);
			FRDGTextureUAVRef SampleDiffuseLightingUAVSkipBarrier = GraphBuilder.CreateUAV(SampleDiffuseLighting, ERDGUnorderedAccessViewFlags::SkipBarrier);
			for (const int32 StandaloneLightIndex : LightingTaskData->StandaloneLightIndices)
			{
				const FLumenGatheredLight& Light = LightingTaskData->GatheredLights[StandaloneLightIndex];
				const bool bMayUseCloudTransmittance = GLumenDirectLightingCloudTransmittance != 0 && Light.bMayCastCloudTransmittance;
				//check(Light.NeedsShadowMask());

				// Two possible cases:
				// * Directional/Local Light with material light functions
				// * Directional light with cloud transmittance
				if (const FMaterialRenderProxy* LightFunctionMaterialProxy = Light.LightFunctionMaterialProxy)
				{					
					FLumenSceneEvaluateStandaloneLightMaterialCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneEvaluateStandaloneLightMaterialCS::FParameters>();
					PassParameters->IndirectArgs = CardTilePerLightArgs;
					PassParameters->LightIndex = StandaloneLightIndex;
					PassParameters->ViewIndex = 0;  // TODO ViewIndex;
					PassParameters->CardTilePerLightCounters = GraphBuilder.CreateSRV(CardTilePerLightCounters);
					PassParameters->CardTilePerLightOffsets = GraphBuilder.CreateSRV(CardTilePerLightOffsets);
					PassParameters->CardTilePerLightDatas = GraphBuilder.CreateSRV(CardTilePerLightDatas);
					PassParameters->LumenSceneData = SceneData;
					PassParameters->RWLightSamples = LightSamplesUAVSkipBarrier;
					PassParameters->RWSampleDiffuseLighting = SampleDiffuseLightingUAVSkipBarrier;
					PassParameters->LumenLightData = LumenLightData;
					PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
					const bool bUseCloudTransmittance = SetupLightCloudTransmittanceParameters(
						GraphBuilder,
						Scene,
						View,
						bMayUseCloudTransmittance ? Light.LightSceneInfo : nullptr,
						PassParameters->LightCloudTransmittanceParameters);
					SetupLightFunctionParameters(View, Light.LightSceneInfo, 1.0f, PassParameters->LightFunctionParameters);

					for (int32 OriginIndex = 0; OriginIndex < NumViewOrigins; ++OriginIndex)
					{
						const FLumenViewOrigin& ViewOrigin = FrameTemporaries.ViewOrigins[OriginIndex];

						PassParameters->PreViewTranslationHigh[OriginIndex] = ViewOrigin.PreViewTranslationDF.High;
						PassParameters->PreViewTranslationLow[OriginIndex] = ViewOrigin.PreViewTranslationDF.Low;
						PassParameters->ViewExposure[OriginIndex] = ViewOrigin.LastEyeAdaptationExposure;
					}

					FLumenSceneEvaluateStandaloneLightMaterialCS::FPermutationDomain PermutationVector;
					//PermutationVector.Set<FLumenSceneEvaluateStandaloneLightMaterialCS::FThreadGroupSize32>(Lumen::UseThreadGroupSize32());
					PermutationVector.Set<FLumenSceneEvaluateStandaloneLightMaterialCS::FCloudTransmittance>(bUseCloudTransmittance);

					const FMaterial& Material = LightFunctionMaterialProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), LightFunctionMaterialProxy);
					const FMaterialShaderMap* MaterialShaderMap = Material.GetRenderingThreadShaderMap();
					TShaderRef<FLumenSceneEvaluateStandaloneLightMaterialCS> ComputeShader = MaterialShaderMap->GetShader<FLumenSceneEvaluateStandaloneLightMaterialCS>(PermutationVector);

					const uint32 DispatchIndirectArgOffset = StandaloneLightIndex * sizeof(FRHIDispatchIndirectParameters);
					ClearUnusedGraphResources(ComputeShader, PassParameters, { CardTilePerLightArgs });

					GraphBuilder.AddPass(
						RDG_EVENT_NAME("StandaloneLight::Evaluate(LF,%s)", *Light.Name),
						PassParameters,
						ComputePassFlags,
						[PassParameters, ComputeShader, CardTilePerLightArgs, DispatchIndirectArgOffset, LightFunctionMaterialProxy, &Material, &View](FRDGAsyncTask, FRHIComputeCommandList& RHICmdList)
						{
							CardTilePerLightArgs->MarkResourceAsUsed();
							FComputeShaderUtils::ValidateIndirectArgsBuffer(CardTilePerLightArgs, DispatchIndirectArgOffset);
							FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
							SetComputePipelineState(RHICmdList, ShaderRHI);
							SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);
							ComputeShader->SetParameters(RHICmdList, ShaderRHI, LightFunctionMaterialProxy, Material, View);
							RHICmdList.DispatchIndirectComputeShader(CardTilePerLightArgs->GetIndirectRHICallBuffer(), DispatchIndirectArgOffset);
							UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
						});
				}
				else
				{
					FLumenSceneEvaluateStandaloneLightCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneEvaluateStandaloneLightCS::FParameters>();
					PassParameters->IndirectArgs = CardTilePerLightArgs;
					PassParameters->LightIndex = StandaloneLightIndex;
					PassParameters->ViewIndex = 0;  // TODO ViewIndex;
					PassParameters->CardTilePerLightCounters = GraphBuilder.CreateSRV(CardTilePerLightCounters);
					PassParameters->CardTilePerLightOffsets = GraphBuilder.CreateSRV(CardTilePerLightOffsets);
					PassParameters->CardTilePerLightDatas = GraphBuilder.CreateSRV(CardTilePerLightDatas);
					PassParameters->LumenSceneData = SceneData;
					PassParameters->RWLightSamples = LightSamplesUAVSkipBarrier;
					PassParameters->RWSampleDiffuseLighting = SampleDiffuseLightingUAVSkipBarrier;
					PassParameters->LumenLightData = LumenLightData;
					PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;

					const bool bUseCloudTransmittance = SetupLightCloudTransmittanceParameters(
						GraphBuilder,
						Scene,
						View,
						bMayUseCloudTransmittance ? Light.LightSceneInfo : nullptr,
						PassParameters->LightCloudTransmittanceParameters);

					for (int32 OriginIndex = 0; OriginIndex < NumViewOrigins; ++OriginIndex)
					{
						const FLumenViewOrigin& ViewOrigin = FrameTemporaries.ViewOrigins[OriginIndex];

						PassParameters->PreViewTranslationHigh[OriginIndex] = ViewOrigin.PreViewTranslationDF.High;
						PassParameters->PreViewTranslationLow[OriginIndex] = ViewOrigin.PreViewTranslationDF.Low;
						PassParameters->ViewExposure[OriginIndex] = ViewOrigin.LastEyeAdaptationExposure;
					}

					FLumenSceneEvaluateStandaloneLightCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FLumenSceneEvaluateStandaloneLightCS::FCloudTransmittance>(bUseCloudTransmittance);
					auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneEvaluateStandaloneLightCS>(PermutationVector);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("StandaloneLight::Evaluate(%s)", *Light.Name),
						ComputeShader,
						PassParameters,
						CardTilePerLightArgs,
						StandaloneLightIndex * sizeof(FRHIDispatchIndirectParameters));
				}
			}
		}

		// 2. Trace compaction
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Compact Traces");

			CompactLumenSceneLightsTraces(
				View,
				GraphBuilder,
				LightSamples,
				CompactedLightSampleData,
				CompactedLightSampleAllocator);
		}

		// 3. HW Trace
		{
			RDG_EVENT_SCOPE(GraphBuilder, "HWRT Trace");

			for (int32 OriginIndex = 0; OriginIndex < NumViewOrigins; ++OriginIndex)
			{
				const FViewInfo& LocalView = *FrameTemporaries.ViewOrigins[OriginIndex].ReferenceView;

				FLumenDirectLightingStochasticData StochasticData;
				StochasticData.CompactedLightSampleData = CompactedLightSampleData;
				StochasticData.CompactedLightSampleAllocator = CompactedLightSampleAllocator;
				StochasticData.LightSamples = LightSamples;
				StochasticData.SceneDataTexture = SceneData;

				TraceLumenHardwareRayTracedDirectLightingShadows(
					GraphBuilder,
					Scene,
					LocalView,
					OriginIndex,
					FrameTemporaries,
					StochasticData,
					LumenLightData,
					nullptr,
					nullptr,
					nullptr,
					nullptr,
					nullptr,
					nullptr,
					ComputePassFlags);
			}
		}

		// 4. Shading
		FRDGTextureRef ResolvedDiffuseLighting = nullptr;
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Shading");

			FLumenSceneShadeLightSamplesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneShadeLightSamplesCS::FParameters>();
			PassParameters->DummyZeroForFixingShaderCompilerBug = 0;
			PassParameters->IndirectArgsBuffer = CardTileUpdateContext.DispatchCardTilesIndirectArgs;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
			PassParameters->DiffuseColorBoost = 1.0f / FMath::Max(View.FinalPostProcessSettings.LumenDiffuseColorBoost, 1.0f);
			PassParameters->NumSamplesPerPixel1d = CommonParameters.NumSamplesPerPixel1d;
			PassParameters->AlbedoAtlas = FrameTemporaries.AlbedoAtlas;
			PassParameters->OpacityAtlas = FrameTemporaries.OpacityAtlas;
			PassParameters->EmissiveAtlas = FrameTemporaries.EmissiveAtlas;
			PassParameters->IndirectLightingAtlas = FrameTemporaries.IndirectLightingAtlas;
			PassParameters->BilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RWFinalLightingAtlas = GraphBuilder.CreateUAV(FrameTemporaries.FinalLightingAtlas);
			PassParameters->RWDirectLightingAtlas = GraphBuilder.CreateUAV(ResolvedDirectLightingAtlas);
			const FIntPoint IndirectLightingAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
			PassParameters->IndirectLightingAtlasHalfTexelSize = FVector2f(0.5f / IndirectLightingAtlasSize.X, 0.5f / IndirectLightingAtlasSize.Y);
			PassParameters->TileAllocator = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTileAllocator);
			PassParameters->TileData = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTiles);
			PassParameters->LightSamples = LightSamples;
			PassParameters->SampleDiffuseLighting = SampleDiffuseLighting;
			PassParameters->LumenSceneDebugData = FrameTemporaries.DebugData;
			ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintUniformBuffer);

			FLumenSceneShadeLightSamplesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenSceneShadeLightSamplesCS::FUseLightSamples>(true);
			auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneShadeLightSamplesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CombineLighting CS"),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				CardTileUpdateContext.DispatchCardTilesIndirectArgs,
				(uint32)ELumenDispatchCardTilesIndirectArgsOffset::OneGroupPerCardTile);
		}

		// 5. Temporal accumulation
		if (bTemporal)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "Temporal Filtering");

			const FIntPoint Resolution = FrameTemporaries.DirectLightingAtlas->Desc.Extent;
			FRDGTextureRef DiffuseLightingAndSecondMoment = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Resolution, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Lumen.SceneLighting.DiffuseLightingAndSecondMoment"));

			FRDGTextureRef NumFramesAccumulated = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Resolution, PF_G8, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Lumen.SceneLighting.NumFramesAccumulated"));

			FLumenSceneDenoiserTemporalCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenSceneDenoiserTemporalCS::FParameters>();
			PassParameters->IndirectArgsBuffer = CardTileUpdateContext.DispatchCardTilesIndirectArgs;
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
			PassParameters->SampleLuminanceSumTexture = SampleLuminanceSum;
			PassParameters->ResolvedDirectLightingAtlas = ResolvedDirectLightingAtlas;
			PassParameters->DiffuseLightingAndSecondMomentHistoryTexture = FrameTemporaries.DiffuseLightingAndSecondMomentHistoryAtlas; // DiffuseLightingAndSecondMomentHistory;
			PassParameters->NumFramesAccumulatedHistoryTexture = FrameTemporaries.NumFramesAccumulatedHistoryAtlas;// NumFramesAccumulatedHistory;
			PassParameters->PrevSceneColorPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;
			PassParameters->RWDiffuseLightingAndSecondMoment = GraphBuilder.CreateUAV(DiffuseLightingAndSecondMoment);
			PassParameters->RWNumFramesAccumulated = GraphBuilder.CreateUAV(NumFramesAccumulated);
			PassParameters->TileAllocator = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTileAllocator);
			PassParameters->TileData = GraphBuilder.CreateSRV(CardTileUpdateContext.CardTiles);
			
			const FIntPoint IndirectLightingAtlasSize = LumenSceneData.GetRadiosityAtlasSize();
			PassParameters->IndirectLightingAtlasHalfTexelSize = FVector2f(0.5f / IndirectLightingAtlasSize.X, 0.5f / IndirectLightingAtlasSize.Y);
			PassParameters->AlbedoAtlas = FrameTemporaries.AlbedoAtlas;
			PassParameters->OpacityAtlas = FrameTemporaries.OpacityAtlas;
			PassParameters->EmissiveAtlas = FrameTemporaries.EmissiveAtlas;
			PassParameters->IndirectLightingAtlas = FrameTemporaries.IndirectLightingAtlas;
			PassParameters->BilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParameters->RWFinalLightingAtlas = GraphBuilder.CreateUAV(FrameTemporaries.FinalLightingAtlas);
			PassParameters->RWDirectLightingAtlas = GraphBuilder.CreateUAV(FrameTemporaries.DirectLightingAtlas);

			FLumenSceneDenoiserTemporalCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenSceneDenoiserTemporalCS::FValidHistory>(FrameTemporaries.DiffuseLightingAndSecondMomentHistoryAtlas != nullptr && bTemporal);
			auto ComputeShader = View.ShaderMap->GetShader<FLumenSceneDenoiserTemporalCS>(PermutationVector);

			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FLumenSceneDenoiserTemporalCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TemporalAccumulation"),
				ComputeShader,
				PassParameters,
				CardTileUpdateContext.DispatchCardTilesIndirectArgs,
				(uint32)ELumenDispatchCardTilesIndirectArgsOffset::OneGroupPerCardTile);


			FLumenSceneFrameTemporaries* NonCstFrameTemporaries = const_cast<FLumenSceneFrameTemporaries*>(&FrameTemporaries);
			if (DiffuseLightingAndSecondMoment && NumFramesAccumulated && bTemporal && NonCstFrameTemporaries)
			{
				NonCstFrameTemporaries->DiffuseLightingAndSecondMomentHistoryAtlas = DiffuseLightingAndSecondMoment;
				NonCstFrameTemporaries->NumFramesAccumulatedHistoryAtlas = NumFramesAccumulated;
			}
			else
			{
				NonCstFrameTemporaries->DiffuseLightingAndSecondMomentHistoryAtlas = nullptr;
				NonCstFrameTemporaries->NumFramesAccumulatedHistoryAtlas = nullptr;
			}
		}

		// Draw direct lighting stats & Lumen cards/tiles
		if (GetLumenLightingStatMode() == 3)
		{
			AddLumenSceneDirectLightingStatsPass(
				GraphBuilder,
				Scene,
				View,
				FrameTemporaries,
				LightingTaskData,
				CardUpdateContext,
				CardTileUpdateContext,
				CompactedLightSampleAllocator,
				ComputePassFlags);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////