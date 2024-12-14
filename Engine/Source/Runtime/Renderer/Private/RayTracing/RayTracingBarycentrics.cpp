// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"

#if RHI_RAYTRACING

#include "BuiltInRayTracingShaders.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "DeferredShadingRenderer.h"
#include "GlobalShader.h"
#include "PostProcess/SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "PipelineStateCache.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingScene.h"
#include "ScenePrivate.h"
#include "RayTracing/RayTracing.h"

#include "Rendering/NaniteStreamingManager.h"

class FRayTracingBarycentricsRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBarycentricsRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingBarycentricsRGS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::Default;
	}

	static const FShaderBindingLayout* GetShaderBindingLayout(const FShaderPermutationParameters& Parameters)
	{
		return RayTracing::GetShaderBindingLayout(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingBarycentricsRGS, "/Engine/Private/RayTracing/RayTracingBarycentrics.usf", "RayTracingBarycentricsMainRGS", SF_RayGen);

// Example closest hit shader
class FRayTracingBarycentricsCHS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBarycentricsCHS);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::Default;
	}

	static const FShaderBindingLayout* GetShaderBindingLayout(const FShaderPermutationParameters& Parameters)
	{
		return RayTracing::GetShaderBindingLayout(Parameters.Platform);
	}

	FRayTracingBarycentricsCHS() = default;
	FRayTracingBarycentricsCHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};
IMPLEMENT_SHADER_TYPE(, FRayTracingBarycentricsCHS, TEXT("/Engine/Private/RayTracing/RayTracingBarycentrics.usf"), TEXT("RayTracingBarycentricsMainCHS"), SF_RayHitGroup);

class FRayTracingBarycentricsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBarycentricsCS)
	SHADER_USE_PARAMETER_STRUCT(FRayTracingBarycentricsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteRasterUniformParameters, NaniteRasterUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteShadingUniformParameters, NaniteShadingUniformBuffer)
		SHADER_PARAMETER(float, RTDebugVisualizationNaniteCutError)
	END_SHADER_PARAMETER_STRUCT()

	class FSupportProceduralPrimitive : SHADER_PERMUTATION_BOOL("ENABLE_TRACE_RAY_INLINE_PROCEDURAL_PRIMITIVE");
	using FPermutationDomain = TShaderPermutationDomain<FSupportProceduralPrimitive>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);

		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_RASTER_UNIFORM_BUFFER"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_SHADING_UNIFORM_BUFFER"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsRayTracingEnabledForProject(Parameters.Platform) && RHISupportsRayTracing(Parameters.Platform) && RHISupportsInlineRayTracing(Parameters.Platform);
	}

	// Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 4;
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingBarycentricsCS, "/Engine/Private/RayTracing/RayTracingBarycentrics.usf", "RayTracingBarycentricsMainCS", SF_Compute);

void RenderRayTracingBarycentricsCS(FRDGBuilder& GraphBuilder, const FScene& Scene, const FViewInfo& View, FRDGTextureRef SceneColor, bool bVisualizeProceduralPrimitives)
{
	FRayTracingBarycentricsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingBarycentricsCS::FParameters>();

	PassParameters->TLAS = View.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
	PassParameters->Output = GraphBuilder.CreateUAV(SceneColor);
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
	PassParameters->NaniteRasterUniformBuffer = CreateDebugNaniteRasterUniformBuffer(GraphBuilder, Scene.GPUScene.InstanceSceneDataSOAStride);
	PassParameters->NaniteShadingUniformBuffer = CreateDebugNaniteShadingUniformBuffer(GraphBuilder);

	PassParameters->RTDebugVisualizationNaniteCutError = 0.0f;

	FIntRect ViewRect = View.ViewRect;

	FRayTracingBarycentricsCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRayTracingBarycentricsCS::FSupportProceduralPrimitive>(bVisualizeProceduralPrimitives);

	auto ComputeShader = View.ShaderMap->GetShader<FRayTracingBarycentricsCS>(PermutationVector);

	const FIntPoint GroupSize(FRayTracingBarycentricsCS::ThreadGroupSizeX, FRayTracingBarycentricsCS::ThreadGroupSizeY);
	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ViewRect.Size(), GroupSize);

	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Barycentrics"), ComputeShader, PassParameters, GroupCount);
}

void RenderRayTracingBarycentricsRGS(FRDGBuilder& GraphBuilder, const FScene& Scene, const FViewInfo& View, FRDGTextureRef SceneColor)
{
	const FRayTracingScene& RayTracingScene = Scene.RayTracingScene;

	auto RayGenShader = View.ShaderMap->GetShader<FRayTracingBarycentricsRGS>();
	auto ClosestHitShader = View.ShaderMap->GetShader<FRayTracingBarycentricsCHS>();

	FRayTracingPipelineStateInitializer Initializer;

	const FShaderBindingLayout* ShaderBindingLayout = RayTracing::GetShaderBindingLayout(View.GetShaderPlatform());
	if (ShaderBindingLayout)
	{
		Initializer.ShaderBindingLayout = &ShaderBindingLayout->RHILayout;
	}

	FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader.GetRayTracingShader() };
	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	FRHIRayTracingShader* HitGroupTable[] = { ClosestHitShader.GetRayTracingShader() };
	Initializer.SetHitGroupTable(HitGroupTable);	

	FRHIRayTracingShader* MissTable[] = { View.ShaderMap->GetShader<FDefaultPayloadMS>().GetRayTracingShader() };
	Initializer.SetMissShaderTable(MissTable);

	FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(GraphBuilder.RHICmdList, Initializer);

	FShaderBindingTableRHIRef SBT = Scene.RayTracingSBT.AllocateRHI(GraphBuilder.RHICmdList, ERayTracingShaderBindingMode::RTPSO, ERayTracingHitGroupIndexingMode::Disallow, RayTracingScene.NumMissShaderSlots, RayTracingScene.NumCallableShaderSlots, Initializer.GetMaxLocalBindingDataSize());
	   
	FRayTracingBarycentricsRGS::FParameters* RayGenParameters = GraphBuilder.AllocParameters<FRayTracingBarycentricsRGS::FParameters>();

	RayGenParameters->TLAS = View.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
	RayGenParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	RayGenParameters->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
	RayGenParameters->Output = GraphBuilder.CreateUAV(SceneColor);

	FIntRect ViewRect = View.ViewRect;
	
	FRHIUniformBuffer* SceneUniformBuffer = View.GetSceneUniforms().GetBufferRHI(GraphBuilder);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Barycentrics"),
		RayGenParameters,
		ERDGPassFlags::Compute,
		[RayGenParameters, RayGenShader, &View, SceneUniformBuffer, SBT, Pipeline, ViewRect](FRDGAsyncTask, FRHICommandList& RHICmdList)
	{
		FRHIBatchedShaderParameters& GlobalResources = RHICmdList.GetScratchShaderParameters();
		SetShaderParameters(GlobalResources, RayGenShader, *RayGenParameters);
		TOptional<FScopedUniformBufferStaticBindings> StaticUniformBufferScope = RayTracing::BindStaticUniformBufferBindings(View, SceneUniformBuffer, RHICmdList);

		// Dispatch rays using default shader binding table
		RHICmdList.SetDefaultRayTracingHitGroup(SBT, Pipeline, 0);
		RHICmdList.SetRayTracingMissShader(SBT, 0, Pipeline, 0 /* ShaderIndexInPipeline */, 0, nullptr, 0);
		RHICmdList.CommitShaderBindingTable(SBT);
		RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), SBT, GlobalResources, ViewRect.Size().X, ViewRect.Size().Y);
	});
}

void FDeferredShadingSceneRenderer::RenderRayTracingBarycentrics(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, bool bVisualizeProceduralPrimitives)
{
	const bool bRayTracingInline = ShouldRenderRayTracingEffect(true, ERayTracingPipelineCompatibilityFlags::Inline, View);
	const bool bRayTracingPipeline = ShouldRenderRayTracingEffect(true, ERayTracingPipelineCompatibilityFlags::FullPipeline, View);

	if (bRayTracingInline)
	{
		RenderRayTracingBarycentricsCS(GraphBuilder, *Scene, View, SceneColor, bVisualizeProceduralPrimitives);
	}
	else if (bRayTracingPipeline)
	{
		RenderRayTracingBarycentricsRGS(GraphBuilder, *Scene, View, SceneColor);
	}
}
#endif
