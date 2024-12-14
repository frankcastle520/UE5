// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "Lumen/LumenScreenProbeGather.h"
#include "MegaLights/MegaLights.h"

class FStochasticLightingStoreSceneHistoryCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStochasticLightingStoreSceneHistoryCS)
	SHADER_USE_PARAMETER_STRUCT(FStochasticLightingStoreSceneHistoryCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWNormalTexture)
	END_SHADER_PARAMETER_STRUCT()

	class FStoreNormal : SHADER_PERMUTATION_BOOL("PERMUTATION_STORE_NORMAL");
	using FPermutationDomain = TShaderPermutationDomain<FStoreNormal>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform) || MegaLights::ShouldCompileShaders(Parameters.Platform);
	}

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

IMPLEMENT_GLOBAL_SHADER(FStochasticLightingStoreSceneHistoryCS, "/Engine/Private/StochasticLighting/StochasticLightingDenoising.usf", "StoreSceneHistoryCS", SF_Compute);

/**
 * Copy depth and normal for opaque before it gets possibly overwritten by water or other translucency writing depth
 */
void FDeferredShadingSceneRenderer::StoreStochasticLightingSceneHistory(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries, const FSceneTextures& SceneTextures)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		if (View.ViewState && !View.bStatePrevViewInfoIsReadOnly)
		{
			const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);
			const FSceneTextureParameters& SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTextures);
			const bool bStoreDepth = ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen || ViewPipelineState.ReflectionsMethod == EReflectionsMethod::Lumen || MegaLights::IsEnabled(ViewFamily);
			const bool bStoreNormal = (ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen && LumenScreenProbeGather::UseRejectBasedOnNormal()) || MegaLights::IsEnabled(ViewFamily);

			if (bStoreDepth)
			{
				FRDGTextureRef DepthHistory = FrameTemporaries.DepthHistory.CreateSharedRT(GraphBuilder,
					FRDGTextureDesc::Create2D(SceneTextures.Config.Extent, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
					FrameTemporaries.ViewExtent,
					TEXT("StochasticLighting.DepthHistory"));

				FRDGTextureRef NormalHistory = bStoreNormal ? FrameTemporaries.NormalHistory.CreateSharedRT(GraphBuilder,
					FRDGTextureDesc::Create2D(SceneTextures.Config.Extent, PF_A2B10G10R10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV),
					FrameTemporaries.ViewExtent,
					TEXT("StochasticLighting.NormalAndShadingInfoHistory")) : nullptr;

				FStochasticLightingStoreSceneHistoryCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FStochasticLightingStoreSceneHistoryCS::FStoreNormal>(bStoreNormal);
				auto ComputeShader = View.ShaderMap->GetShader<FStochasticLightingStoreSceneHistoryCS>(PermutationVector);

				FStochasticLightingStoreSceneHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStochasticLightingStoreSceneHistoryCS::FParameters>();
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
				PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
				PassParameters->RWDepthTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DepthHistory));
				PassParameters->RWNormalTexture = NormalHistory ? GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NormalHistory)) : nullptr;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("StochasticLightingStoreSceneHistory Normal:%d", bStoreNormal ? 1 : 0),
					ComputeShader,
					PassParameters,
					FComputeShaderUtils::GetGroupCount(View.ViewRect.Size(), FStochasticLightingStoreSceneHistoryCS::GetGroupSize()));
			}
		}
	}
}

void FDeferredShadingSceneRenderer::QueueExtractStochasticLighting(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		if (View.ViewState && !View.bStatePrevViewInfoIsReadOnly)
		{
			if (FrameTemporaries.DepthHistory.GetRenderTarget())
			{
				GraphBuilder.QueueTextureExtraction(FrameTemporaries.DepthHistory.GetRenderTarget(), &View.ViewState->StochasticLighting.SceneDepthHistory);
			}
			else
			{
				View.ViewState->StochasticLighting.SceneDepthHistory = nullptr;
			}

			if (FrameTemporaries.NormalHistory.GetRenderTarget())
			{
				GraphBuilder.QueueTextureExtraction(FrameTemporaries.NormalHistory.GetRenderTarget(), &View.ViewState->StochasticLighting.SceneNormalHistory);
			}
			else
			{
				View.ViewState->StochasticLighting.SceneNormalHistory = nullptr;
			}
		}
	}
}