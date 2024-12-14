// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessCompositeEditorPrimitives.h"

#if WITH_EDITOR
#include "EditorPrimitivesRendering.h"
#include "MeshPassProcessor.inl"
#include "BasePassRendering.h"
#include "MobileBasePassRendering.h"
#include "PixelShaderUtils.h"
#include "Substrate/Substrate.h"
#include "MeshEdgesRendering.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessing.h" // IsPostProcessingWithAlphaChannelSupported

namespace
{
class FCompositeEditorPrimitivesPS : public FCompositePrimitiveShaderBase
{
public:
	class FWriteDepth : SHADER_PERMUTATION_BOOL("WRITE_DEPTH");
					
	using FPermutationDomain = TShaderPermutationDomain<FWriteDepth, FSampleCountDimension, FMSAADontResolve>;
			
	DECLARE_GLOBAL_SHADER(FCompositeEditorPrimitivesPS);
	SHADER_USE_PARAMETER_STRUCT(FCompositeEditorPrimitivesPS, FCompositePrimitiveShaderBase);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Depth)
		SHADER_PARAMETER_ARRAY(FVector4f, SampleOffsetArray, [FCompositePrimitiveShaderBase::kMSAASampleCountMax])

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, UndistortingDisplacementTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  UndistortingDisplacementSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EditorPrimitivesDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EditorPrimitivesColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  ColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState,  DepthSampler)

		SHADER_PARAMETER(FScreenTransform, PassSvPositionToViewportUV)
		SHADER_PARAMETER(FScreenTransform, ViewportUVToColorUV)
		SHADER_PARAMETER(FScreenTransform, ViewportUVToDepthUV)
		SHADER_PARAMETER(uint32, bOpaqueEditorGizmo)
		SHADER_PARAMETER(uint32, bCompositeAnyNonNullDepth)
		SHADER_PARAMETER(FVector2f, DepthTextureJitter)
		SHADER_PARAMETER(uint32, bProcessAlpha)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FPermutationDomain& PermutationVector, const EShaderPlatform Platform)
	{
		const int32 SampleCount = PermutationVector.Get<FSampleCountDimension>();
		// Only use permutations with valid MSAA sample counts.
		if (!FMath::IsPowerOfTwo(SampleCount))
		{
		   return false;
		}

		return IsPCPlatform(Platform);
	}
	   
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
	   const FPermutationDomain PermutationVector(Parameters.PermutationId);
	   return ShouldCompilePermutation(PermutationVector, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FCompositeEditorPrimitivesPS, "/Engine/Private/PostProcessCompositePrimitives.usf", "MainCompositeEditorPrimitivesPS", SF_Pixel);


void RenderEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState, FInstanceCullingManager& InstanceCullingManager)
{
	// Always depth test against other editor primitives
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<
		true, CF_DepthNearOrEqual,
		true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
		false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
		0xFF, GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1) | STENCIL_LIGHTING_CHANNELS_MASK(0x7)>::GetRHI());

	DrawDynamicMeshPass(View, RHICmdList,
		[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
			View.Family->Scene->GetRenderScene(),
			View.GetFeatureLevel(),
			&View,
			DrawRenderState,
			false,
			DynamicMeshPassContext);

		const uint64 DefaultBatchElementMask = ~0ull;
		const int32 NumDynamicEditorMeshBatches = View.DynamicEditorMeshElements.Num();

		for (int32 MeshIndex = 0; MeshIndex < NumDynamicEditorMeshBatches; MeshIndex++)
		{
			const FMeshBatchAndRelevance& MeshAndRelevance = View.DynamicEditorMeshElements[MeshIndex];

			if (MeshAndRelevance.GetHasOpaqueOrMaskedMaterial() || View.Family->EngineShowFlags.Wireframe)
			{
				PassMeshProcessor.AddMeshBatch(*MeshAndRelevance.Mesh, DefaultBatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
			}
		}

		for (int32 MeshIndex = 0; MeshIndex < View.ViewMeshElements.Num(); MeshIndex++)
		{
			const FMeshBatch& MeshBatch = View.ViewMeshElements[MeshIndex];
			PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
		}
	});

	View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_World);

	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];

	// Draw the view's batched simple elements(lines, sprites, etc).
	View.BatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, View, false, 1.0f);
}

void RenderForegroundTranslucentEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState, FInstanceCullingManager& InstanceCullingManager)
{
	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];

	// Force all translucent editor primitives to standard translucent rendering
	const ETranslucencyPass::Type TranslucencyPass = ETranslucencyPass::TPT_TranslucencyStandard;

	if (TranslucencyPass == ETranslucencyPass::TPT_TranslucencyStandard)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	}

	View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::Translucent, SDPG_Foreground);

	DrawDynamicMeshPass(View, RHICmdList,
		[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				View.GetFeatureLevel(),
				&View,
				DrawRenderState,
				true,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ull;

			for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
			{
				const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
				PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
			}
		});
	
	View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, View, false, 1.0f, EBlendModeFilter::Translucent);
}

void RenderForegroundEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, FMeshPassProcessorRenderState& DrawRenderState, FInstanceCullingManager& InstanceCullingManager)
{
	const auto FeatureLevel = View.GetFeatureLevel();
	const auto ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];

	// Draw a first time the foreground primitive without depth test to over right depth from non-foreground editor primitives.
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_Always>::GetRHI());

		View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				View.GetFeatureLevel(),
				&View,
				DrawRenderState,
				false,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ull;

			for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
			{
				const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
				PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
			}
		});

		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, View, false);
	}

	// Draw a second time the foreground primitive with depth test to have proper depth test between foreground primitives.
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());

		View.EditorSimpleElementCollector.DrawBatchedElements(RHICmdList, DrawRenderState, View, EBlendModeFilter::OpaqueAndMasked, SDPG_Foreground);

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, &DrawRenderState](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FEditorPrimitivesBasePassMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				View.GetFeatureLevel(),
				&View,
				DrawRenderState,
				false,
				DynamicMeshPassContext);

			const uint64 DefaultBatchElementMask = ~0ull;

			for (int32 MeshIndex = 0; MeshIndex < View.TopViewMeshElements.Num(); MeshIndex++)
			{
				const FMeshBatch& MeshBatch = View.TopViewMeshElements[MeshIndex];
				PassMeshProcessor.AddMeshBatch(MeshBatch, DefaultBatchElementMask, nullptr);
			}
		});

		View.TopBatchedViewElements.Draw(RHICmdList, DrawRenderState, FeatureLevel, View, false);
	}
}

} //! namespace

BEGIN_SHADER_PARAMETER_STRUCT(FEditorPrimitivesPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
	SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
	SHADER_PARAMETER_STRUCT_REF(FMobileReflectionCaptureShaderData, MobileReflectionCaptureData)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTranslucentBasePassUniformParameters, TranslucentBasePass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileBasePassUniformParameters, MobileBasePass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FScreenPassTexture AddEditorPrimitivePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FCompositePrimitiveInputs& Inputs,
	FInstanceCullingManager& InstanceCullingManager)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.SceneDepth.IsValid());
	check(Inputs.BasePassType != FCompositePrimitiveInputs::EBasePassType::MAX);

	const FSceneTextures& SceneTextures = View.GetSceneTextures();
	const uint32 NumMSAASamples = SceneTextures.Config.EditorPrimitiveNumSamples;
	const FViewInfo* EditorView = CreateCompositePrimitiveView(View, Inputs.SceneColor.ViewRect, NumMSAASamples);

	// Load the color target if it already exists.
	bool bProducedByPriorPass = HasBeenProduced(SceneTextures.EditorPrimitiveColor);
	FIntPoint Extent = Inputs.SceneColor.Texture->Desc.Extent;
	FRDGTextureRef EditorPrimitiveColor;
	FRDGTextureRef EditorPrimitiveDepth;
	if (bProducedByPriorPass)
	{
		EditorPrimitiveColor = SceneTextures.EditorPrimitiveColor;
	}
	else
	{
		const FRDGTextureDesc ColorDesc = FRDGTextureDesc::Create2D(
			Extent,
			PF_B8G8R8A8,
			FClearValueBinding::Transparent,
			TexCreate_ShaderResource | TexCreate_RenderTargetable,
			1,
			NumMSAASamples);

		EditorPrimitiveColor = GraphBuilder.CreateTexture(ColorDesc, TEXT("Editor.PrimitivesColor"));
	}

	if (bProducedByPriorPass && Inputs.SceneColor.ViewRect == Inputs.SceneDepth.ViewRect)
	{
		EditorPrimitiveDepth = SceneTextures.EditorPrimitiveDepth;
	}
	else
	{
		EditorPrimitiveDepth = CreateCompositeDepthTexture(GraphBuilder, Extent, NumMSAASamples);
		//bProducedByPriorPass no longer true as this pass had to create a depth texture for temporal upscaling
		bProducedByPriorPass = false;
	}
	
	// Subtrate data might not be produced in certain case (e.g., path-tracer). In such a case we force generate 
	// them with a simple clear to please validation.
	if (Substrate::IsSubstrateEnabled() && Substrate::UsesSubstrateMaterialBuffer(View.GetShaderPlatform()) && !HasBeenProduced(View.SubstrateViewData.SceneData->TopLayerTexture))
	{
		FRDGTextureClearInfo ClearInfo;
		AddClearRenderTargetPass(GraphBuilder, View.SubstrateViewData.SceneData->TopLayerTexture, ClearInfo);
	}

	// Load the color target if it already exists.
	const FScreenPassTextureViewport EditorPrimitivesViewport(EditorPrimitiveColor, Inputs.SceneColor.ViewRect);

	RDG_EVENT_SCOPE_STAT(GraphBuilder, EditorPrimitives, "CompositeEditorPrimitives %dx%d MSAA=%d",
		EditorPrimitivesViewport.Rect.Width(),
		EditorPrimitivesViewport.Rect.Height(),
		NumMSAASamples);
	RDG_GPU_STAT_SCOPE(GraphBuilder, EditorPrimitives);

	//Inputs is const so create a over-ridable texture reference
	FScreenPassTexture SceneDepth = Inputs.SceneDepth;
	FVector2f SceneDepthJitter = FVector2f(View.TemporalJitterPixels);

	// The editor primitive composition pass is also used when rendering VMI_WIREFRAME in order to use MSAA.
	// So we need to check whether the editor primitives are enabled inside this function.
	if (View.Family->EngineShowFlags.CompositeEditorPrimitives || View.Family->EngineShowFlags.MeshEdges)
	{
		// Populate depth if a prior pass did not already do it.
		if (!bProducedByPriorPass)
		{
			if (IsTemporalAccumulationBasedMethod(View.AntiAliasingMethod))
			{
				TemporalUpscaleDepthPass(GraphBuilder,
					*EditorView,
					Inputs.SceneColor,
					SceneDepth,
					SceneDepthJitter);
			}

			PopulateDepthPass(GraphBuilder,
				*EditorView,
				Inputs.SceneColor,
				SceneDepth,
				EditorPrimitiveColor,
				EditorPrimitiveDepth,
				SceneDepthJitter,
				NumMSAASamples);
		}	
		
		FScreenPassRenderTarget EditorPrimitiveColorRT(EditorPrimitiveColor, EditorPrimitivesViewport.Rect, ERenderTargetLoadAction::ELoad);
		FScreenPassRenderTarget EditorPrimitiveDepthRT(EditorPrimitiveDepth, EditorPrimitivesViewport.Rect, ERenderTargetLoadAction::ELoad);
		ComposeMeshEdges(GraphBuilder, View, EditorPrimitiveColorRT, EditorPrimitiveDepthRT);

		// Draws the editors opaque primitives
		{
			FEditorPrimitivesPassParameters* PassParameters = GraphBuilder.AllocParameters<FEditorPrimitivesPassParameters>();
			PassParameters->View = EditorView->GetShaderParameters();
			PassParameters->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
			PassParameters->ReflectionCapture = View.ReflectionCaptureUniformBuffer;
			PassParameters->MobileReflectionCaptureData = View.MobileReflectionCaptureUniformBuffer;
			PassParameters->InstanceCulling = InstanceCullingManager.GetDummyInstanceCullingUniformBuffer();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(EditorPrimitiveColor, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(EditorPrimitiveDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			const FCompositePrimitiveInputs::EBasePassType BasePassType = Inputs.BasePassType;

			if (BasePassType == FCompositePrimitiveInputs::EBasePassType::Deferred)
			{
				PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, *EditorView, 0);
			}
			else
			{
				PassParameters->MobileBasePass = CreateMobileBasePassUniformBuffer(GraphBuilder, *EditorView, EMobileBasePass::Translucent, EMobileSceneTextureSetupMode::None);
			}

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("EditorPrimitives %dx%d MSAA=%d",
					EditorPrimitivesViewport.Rect.Width(),
					EditorPrimitivesViewport.Rect.Height(),
					NumMSAASamples),
				PassParameters,
				ERDGPassFlags::Raster,
				[&View, &InstanceCullingManager, PassParameters, EditorView, EditorPrimitivesViewport, BasePassType, NumMSAASamples](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(EditorPrimitivesViewport.Rect.Min.X, EditorPrimitivesViewport.Rect.Min.Y, 0.0f, EditorPrimitivesViewport.Rect.Max.X, EditorPrimitivesViewport.Rect.Max.Y, 1.0f);

				FMeshPassProcessorRenderState DrawRenderState;
				DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
				DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());

				// Draw editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), NumMSAASamples);

					RenderEditorPrimitives(RHICmdList, *EditorView, DrawRenderState, InstanceCullingManager);
				}

				// Draw foreground editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorForegroundPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), NumMSAASamples);

					RenderForegroundEditorPrimitives(RHICmdList, *EditorView, DrawRenderState, InstanceCullingManager);
				}
			});
		}
	}

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	FScreenPassRenderTarget DepthOutput = Inputs.OverrideDepthOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, View.GetOverwriteLoadAction(), TEXT("Editor.Primitives"));
	}

	{
		FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		const bool bOpaqueEditorGizmo = View.Family->EngineShowFlags.OpaqueCompositeEditorPrimitives || View.Family->EngineShowFlags.Wireframe;

		FCompositeEditorPrimitivesPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompositeEditorPrimitivesPS::FParameters>();
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		bool bOutputIsMSAA = Output.Texture->Desc.NumSamples > 1;
		if (DepthOutput.IsValid())
		{
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthOutput.Texture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite);
			verify(Output.Texture->Desc.NumSamples == DepthOutput.Texture->Desc.NumSamples);
		}

		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->Color = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(Inputs.SceneColor));
		PassParameters->Depth = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(SceneDepth));

		PassParameters->UndistortingDisplacementTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
		PassParameters->UndistortingDisplacementSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		if (Inputs.LensDistortionLUT.IsEnabled())
		{
			PassParameters->UndistortingDisplacementTexture = Inputs.LensDistortionLUT.UndistortingDisplacementTexture;
		}

		PassParameters->ColorTexture = Inputs.SceneColor.Texture;
		PassParameters->ColorSampler = PointClampSampler;
		if (View.Family->EngineShowFlags.SceneCaptureCopySceneDepth)
		{
			PassParameters->DepthTexture = SceneDepth.Texture;
		}
		else
		{
			PassParameters->DepthTexture = GSystemTextures.GetDepthDummy(GraphBuilder);
		}
		PassParameters->DepthSampler = PointClampSampler;
		PassParameters->EditorPrimitivesDepth = EditorPrimitiveDepth;
		PassParameters->EditorPrimitivesColor = EditorPrimitiveColor;
		
		PassParameters->PassSvPositionToViewportUV = FScreenTransform::SvPositionToViewportUV(Output.ViewRect);
		PassParameters->ViewportUVToColorUV = FScreenTransform::ChangeTextureBasisFromTo(
			FScreenPassTextureViewport(Inputs.SceneColor), FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV);
		PassParameters->ViewportUVToDepthUV = FScreenTransform::ChangeTextureBasisFromTo(
			FScreenPassTextureViewport(SceneDepth), FScreenTransform::ETextureBasis::ViewportUV, FScreenTransform::ETextureBasis::TextureUV);
		
		PassParameters->bOpaqueEditorGizmo = bOpaqueEditorGizmo;
		PassParameters->bCompositeAnyNonNullDepth = bProducedByPriorPass && !View.Family->EngineShowFlags.MeshEdges;
		PassParameters->DepthTextureJitter = SceneDepthJitter;
		PassParameters->bProcessAlpha = IsPostProcessingWithAlphaChannelSupported();

		for (int32 i = 0; i < int32(NumMSAASamples); i++)
		{
			PassParameters->SampleOffsetArray[i].X = GetMSAASampleOffsets(NumMSAASamples, i).X;
			PassParameters->SampleOffsetArray[i].Y = GetMSAASampleOffsets(NumMSAASamples, i).Y;
		}

		FCompositeEditorPrimitivesPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FCompositeEditorPrimitivesPS::FSampleCountDimension>(NumMSAASamples);
		PermutationVector.Set<FCompositeEditorPrimitivesPS::FMSAADontResolve>(bOutputIsMSAA);
		PermutationVector.Set<FCompositeEditorPrimitivesPS::FWriteDepth>(DepthOutput.IsValid());

		TShaderMapRef<FCompositeEditorPrimitivesPS> PixelShader(View.ShaderMap, PermutationVector);
		
		FRHIDepthStencilState* DepthStencilState = nullptr;
		if (DepthOutput.IsValid())
		{
			DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
		}

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("Composite %dx%d MSAA=%d", Output.ViewRect.Width(), Output.ViewRect.Height(), NumMSAASamples),
			PixelShader,
			PassParameters,
			Output.ViewRect,
			nullptr,
			nullptr,
			DepthStencilState);
	}

	// Draws the editor translucent primitives on top of the opaque scene primitives
	if (View.Family->EngineShowFlags.CompositeEditorPrimitives && View.bHasTranslucentViewMeshElements)
	{
		FEditorPrimitivesPassParameters* PassParameters = GraphBuilder.AllocParameters<FEditorPrimitivesPassParameters>();
		PassParameters->View = EditorView->GetShaderParameters();
		PassParameters->Scene = View.GetSceneUniforms().GetBuffer(GraphBuilder);
		PassParameters->ReflectionCapture = View.ReflectionCaptureUniformBuffer;
		PassParameters->MobileReflectionCaptureData = View.MobileReflectionCaptureUniformBuffer;
		PassParameters->InstanceCulling = InstanceCullingManager.GetDummyInstanceCullingUniformBuffer();
		PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
		PassParameters->RenderTargets[0].SetLoadAction(ERenderTargetLoadAction::ELoad);

		const FCompositePrimitiveInputs::EBasePassType BasePassType = Inputs.BasePassType;

		if (BasePassType == FCompositePrimitiveInputs::EBasePassType::Deferred)
		{
			PassParameters->TranslucentBasePass = CreateTranslucentBasePassUniformBuffer(GraphBuilder, nullptr, *EditorView, 0);
		}
		else
		{
			PassParameters->MobileBasePass = CreateMobileBasePassUniformBuffer(GraphBuilder, *EditorView, EMobileBasePass::Translucent, EMobileSceneTextureSetupMode::None);
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("EditorPrimitives Translucent %dx%d MSAA=%d",
				EditorPrimitivesViewport.Rect.Width(),
				EditorPrimitivesViewport.Rect.Height(),
				NumMSAASamples),
			PassParameters,
			ERDGPassFlags::Raster,
			[&View, &InstanceCullingManager, PassParameters, EditorView, EditorPrimitivesViewport, OutputViewportRect = Output.ViewRect, BasePassType, NumMSAASamples](FRHICommandList& RHICmdList)
			{
				RHICmdList.SetViewport(OutputViewportRect.Min.X, OutputViewportRect.Min.Y, 0.0f, OutputViewportRect.Max.X, OutputViewportRect.Max.Y, 1.0f);

				FMeshPassProcessorRenderState DrawRenderState;
				DrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilNop);
				DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_RGBA>::GetRHI());

				// Draw foreground editor primitives.
				{
					SCOPED_DRAW_EVENTF(RHICmdList, EditorPrimitives,
						TEXT("RenderViewEditorForegroundTranslucentPrimitives %dx%d msaa=%d"),
						EditorPrimitivesViewport.Rect.Width(), EditorPrimitivesViewport.Rect.Height(), NumMSAASamples);

					RenderForegroundTranslucentEditorPrimitives(RHICmdList, *EditorView, DrawRenderState, InstanceCullingManager);
				}
			});
	}

	return MoveTemp(Output);
}

#endif
