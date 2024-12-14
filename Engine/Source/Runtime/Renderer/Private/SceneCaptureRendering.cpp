// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/
#include "SceneCaptureRendering.h"
#include "Containers/ArrayView.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Misc/MemStack.h"
#include "EngineDefines.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "Engine/Scene.h"
#include "SceneInterface.h"
#include "LegacyScreenPercentageDriver.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "Shader.h"
#include "TextureResource.h"
#include "SceneUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#include "RendererModule.h"
#include "Rendering/MotionVectorSimulation.h"
#include "SceneViewExtension.h"
#include "GenerateMips.h"
#include "RectLightTexture.h"
#include "Materials/MaterialRenderProxy.h"
#include "Rendering/CustomRenderPass.h"
#include "DumpGPU.h"
#include "IRenderCaptureProvider.h"
#include "RenderCaptureInterface.h"
#include "CustomRenderPassSceneCapture.h"

bool GSceneCaptureAllowRenderInMainRenderer = true;
static FAutoConsoleVariableRef CVarSceneCaptureAllowRenderInMainRenderer(
	TEXT("r.SceneCapture.AllowRenderInMainRenderer"),
	GSceneCaptureAllowRenderInMainRenderer,
	TEXT("Whether to allow SceneDepth & DeviceDepth scene capture to render in the main renderer as an optimization.\n")
	TEXT("0: render as an independent renderer.\n")
	TEXT("1: render as part of the main renderer if Render in Main Renderer is enabled on scene capture component.\n"),
	ECVF_Scalability);

bool GSceneCaptureCubeSinglePass = true;
static FAutoConsoleVariableRef CVarSceneCaptureCubeSinglePass(
	TEXT("r.SceneCapture.CubeSinglePass"),
	GSceneCaptureCubeSinglePass,
	TEXT("Whether to run all 6 faces of cube map capture in a single scene renderer pass."),
	ECVF_Scalability);

static int32 GRayTracingSceneCaptures = -1;
static FAutoConsoleVariableRef CVarRayTracingSceneCaptures(
	TEXT("r.RayTracing.SceneCaptures"),
	GRayTracingSceneCaptures,
	TEXT("Enable ray tracing in scene captures.\n")
	TEXT(" -1: Use scene capture settings (default) \n")
	TEXT(" 0: off \n")
	TEXT(" 1: on"),
	ECVF_Default);


#if WITH_EDITOR
// All scene captures on the given render thread frame will be dumped
uint32 GDumpSceneCaptureMemoryFrame = INDEX_NONE;
void DumpSceneCaptureMemory()
{
	ENQUEUE_RENDER_COMMAND(DumpSceneCaptureMemory)(
		[](FRHICommandList& RHICmdList)
		{
			GDumpSceneCaptureMemoryFrame = GFrameNumberRenderThread;
		});
}

FAutoConsoleCommand CmdDumpSceneCaptureViewState(
	TEXT("r.SceneCapture.DumpMemory"),
	TEXT("Editor specific command to dump scene capture memory to log"),
	FConsoleCommandDelegate::CreateStatic(DumpSceneCaptureMemory)
);
#endif  // WITH_EDITOR

/** A pixel shader for capturing a component of the rendered scene for a scene capture.*/
class FSceneCapturePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSceneCapturePS);
	SHADER_USE_PARAMETER_STRUCT(FSceneCapturePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	enum class ESourceMode : uint32
	{
		ColorAndOpacity,
		ColorNoAlpha,
		ColorAndSceneDepth,
		SceneDepth,
		DeviceDepth,
		Normal,
		BaseColor,
		MAX
	};

	class FSourceModeDimension : SHADER_PERMUTATION_ENUM_CLASS("SOURCE_MODE", ESourceMode);
	class FEnable128BitRT : SHADER_PERMUTATION_BOOL("ENABLE_128_BIT");
	using FPermutationDomain = TShaderPermutationDomain<FSourceModeDimension, FEnable128BitRT>;

	static FPermutationDomain GetPermutationVector(ESceneCaptureSource CaptureSource, bool bUse128BitRT, bool bIsMobilePlatform)
	{
		ESourceMode SourceMode = ESourceMode::MAX;
		switch (CaptureSource)
		{
		case SCS_SceneColorHDR:
			SourceMode = ESourceMode::ColorAndOpacity;
			break;
		case SCS_SceneColorHDRNoAlpha:
			SourceMode = ESourceMode::ColorNoAlpha;
			break;
		case SCS_SceneColorSceneDepth:
			SourceMode = ESourceMode::ColorAndSceneDepth;
			break;
		case SCS_SceneDepth:
			SourceMode = ESourceMode::SceneDepth;
			break;
		case SCS_DeviceDepth:
			SourceMode = ESourceMode::DeviceDepth;
			break;
		case SCS_Normal:
			SourceMode = ESourceMode::Normal;
			break;
		case SCS_BaseColor:
			SourceMode = ESourceMode::BaseColor;
			break;
		default:
			checkf(false, TEXT("SceneCaptureSource not implemented."));
		}

		if (bIsMobilePlatform && (SourceMode == ESourceMode::Normal || SourceMode == ESourceMode::BaseColor))
		{
			SourceMode = ESourceMode::ColorAndOpacity;
		}
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FSourceModeDimension>(SourceMode);
		PermutationVector.Set<FEnable128BitRT>(bUse128BitRT);
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		auto SourceModeDim = PermutationVector.Get<FSourceModeDimension>();
		bool bPlatformRequiresExplicit128bitRT = FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(Parameters.Platform);
		return (!PermutationVector.Get<FEnable128BitRT>() || bPlatformRequiresExplicit128bitRT) && (!IsMobilePlatform(Parameters.Platform) || (SourceModeDim != ESourceMode::Normal && SourceModeDim != ESourceMode::BaseColor));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		static const TCHAR* ShaderSourceModeDefineName[] =
		{
			TEXT("SOURCE_MODE_SCENE_COLOR_AND_OPACITY"),
			TEXT("SOURCE_MODE_SCENE_COLOR_NO_ALPHA"),
			TEXT("SOURCE_MODE_SCENE_COLOR_SCENE_DEPTH"),
			TEXT("SOURCE_MODE_SCENE_DEPTH"),
			TEXT("SOURCE_MODE_DEVICE_DEPTH"),
			TEXT("SOURCE_MODE_NORMAL"),
			TEXT("SOURCE_MODE_BASE_COLOR")
		};
		static_assert(UE_ARRAY_COUNT(ShaderSourceModeDefineName) == (uint32)ESourceMode::MAX, "ESourceMode doesn't match define table.");

		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 SourceModeIndex = static_cast<uint32>(PermutationVector.Get<FSourceModeDimension>());
		OutEnvironment.SetDefine(ShaderSourceModeDefineName[SourceModeIndex], 1u);

		if (PermutationVector.Get<FEnable128BitRT>())
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		}

		if (IsMobilePlatform(Parameters.Platform))
		{
			OutEnvironment.FullPrecisionInPS = 1;
		}

	}
};

IMPLEMENT_GLOBAL_SHADER(FSceneCapturePS, "/Engine/Private/SceneCapturePixelShader.usf", "Main", SF_Pixel);

static bool CaptureNeedsSceneColor(ESceneCaptureSource CaptureSource)
{
	return CaptureSource != SCS_FinalColorLDR && CaptureSource != SCS_FinalColorHDR && CaptureSource != SCS_FinalToneCurveHDR;
}

static TFunction<void(FRHICommandList& RHICmdList, int32 ViewIndex)> CopyCaptureToTargetSetViewportFn = [](FRHICommandList& RHICmdList, int32 ViewIndex) {};

void CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef ViewFamilyTexture,
	FRDGTextureRef ViewFamilyDepthTexture,
	const FSceneViewFamily& ViewFamily,
	TConstArrayView<FViewInfo> Views)
{
	TArray<const FViewInfo*> ViewPtrArray;
	for (const FViewInfo& View : Views)
	{
		ViewPtrArray.Add(&View);
	}
	CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, ViewFamilyDepthTexture, ViewFamily, ViewPtrArray);
}

void CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef ViewFamilyTexture,
	FRDGTextureRef ViewFamilyDepthTexture,
	const FSceneViewFamily& ViewFamily,
	const TArray<const FViewInfo*>& Views)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	const bool bForwardShadingEnabled = IsForwardShadingEnabled(ViewFamily.GetShaderPlatform());
	int32 NumViews = Views.Num();
	for (int32 ViewIndex = 0; ViewIndex < NumViews; ViewIndex++)
	{
		const FViewInfo& View = *Views[ViewIndex];

		// If view has its own scene capture setting, use it over view family setting
		ESceneCaptureSource SceneCaptureSource = View.CustomRenderPass ? View.CustomRenderPass->GetSceneCaptureSource() : ViewFamily.SceneCaptureSource;
		if (bForwardShadingEnabled && (SceneCaptureSource == SCS_Normal || SceneCaptureSource == SCS_BaseColor))
		{
			SceneCaptureSource = SCS_SceneColorHDR;
		}
		if (!CaptureNeedsSceneColor(SceneCaptureSource))
		{
			continue;
		}

		RDG_EVENT_SCOPE(GraphBuilder, "CaptureSceneComponent_View[%d]", SceneCaptureSource);

		bool bIsCompositing = false;
		if (SceneCaptureSource == SCS_SceneColorHDR && ViewFamily.SceneCaptureCompositeMode == SCCM_Composite)
		{
			// Blend with existing render target color. Scene capture color is already pre-multiplied by alpha.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
			bIsCompositing = true;
		}
		else if (SceneCaptureSource == SCS_SceneColorHDR && ViewFamily.SceneCaptureCompositeMode == SCCM_Additive)
		{
			// Add to existing render target color. Scene capture color is already pre-multiplied by alpha.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
			bIsCompositing = true;
		}
		else
		{
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		}

		const bool bUse128BitRT = PlatformRequires128bitRT(ViewFamilyTexture->Desc.Format);
		const FSceneCapturePS::FPermutationDomain PixelPermutationVector = FSceneCapturePS::GetPermutationVector(SceneCaptureSource, bUse128BitRT, IsMobilePlatform(ViewFamily.GetShaderPlatform()));

		FSceneCapturePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSceneCapturePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextures.GetSceneTextureShaderParameters(ViewFamily.GetFeatureLevel());
		PassParameters->RenderTargets[0] = FRenderTargetBinding(ViewFamilyTexture, bIsCompositing ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FSceneCapturePS> PixelShader(View.ShaderMap, PixelPermutationVector);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		FIntPoint TargetSize;
		if (((const FViewFamilyInfo*)View.Family)->bIsSceneTextureSizedCapture)
		{
			// Scene texture sized target, use actual target extent for copy, and set correct extent for visualization debug feature
			TargetSize = ViewFamilyTexture->Desc.Extent;
			ViewFamilyTexture->EncloseVisualizeExtent(View.UnconstrainedViewRect.Max);
		}
		else
		{
			// Need to use the extent from the actual target texture for cube captures.  Although perhaps we should use the actual texture
			// extent across the board?  Would it ever be incorrect to do so?
			TargetSize = (View.bIsSceneCaptureCube && NumViews == 6) ? ViewFamilyTexture->Desc.Extent : View.UnconstrainedViewRect.Size();
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("View(%d)", ViewIndex),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, GraphicsPSOInit, VertexShader, PixelShader, &View, ViewIndex, TargetSize] (FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer LocalGraphicsPSOInit = GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(LocalGraphicsPSOInit);
			SetGraphicsPipelineState(RHICmdList, LocalGraphicsPSOInit, 0);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
			
			CopyCaptureToTargetSetViewportFn(RHICmdList, ViewIndex);

			DrawRectangle(
				RHICmdList,
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				TargetSize,
				View.GetSceneTexturesConfig().Extent,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}

	if (ViewFamilyDepthTexture && ViewFamily.EngineShowFlags.SceneCaptureCopySceneDepth)
	{
		verify(SceneTextures.Depth.Target->Desc == ViewFamilyDepthTexture->Desc);
		AddCopyTexturePass(GraphBuilder, SceneTextures.Depth.Target, ViewFamilyDepthTexture);
	}
}

void CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef ViewFamilyTexture,
	FRDGTextureRef ViewFamilyDepthTexture,
	const FSceneViewFamily& ViewFamily,
	TConstStridedView<FSceneView> Views)
{
	const FSceneView& View = Views[0];

	check(View.bIsViewInfo);
	const FMinimalSceneTextures& SceneTextures = static_cast<const FViewInfo&>(View).GetSceneTextures();

	TConstArrayView<FViewInfo> ViewInfos = MakeArrayView(static_cast<const FViewInfo*>(&Views[0]), Views.Num());

	CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, ViewFamilyDepthTexture, ViewFamily, ViewInfos);
}

void CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef ViewFamilyTexture,
	const FSceneViewFamily& ViewFamily,
	const TArray<const FViewInfo*>& Views)
{
	CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, nullptr, ViewFamily, Views);
}

void CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef ViewFamilyTexture,
	const FSceneViewFamily& ViewFamily,
	TConstArrayView<FViewInfo> Views)
{
	CopySceneCaptureComponentToTarget(GraphBuilder, SceneTextures, ViewFamilyTexture, nullptr, ViewFamily, Views);
}

void CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef ViewFamilyTexture,
	const FSceneViewFamily& ViewFamily,
	TConstStridedView<FSceneView> Views)
{
	CopySceneCaptureComponentToTarget(GraphBuilder, ViewFamilyTexture, nullptr, ViewFamily, Views);
}

static void UpdateSceneCaptureContentDeferred_RenderThread(
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer* SceneRenderer, 
	FRenderTarget* RenderTarget, 
	FTexture* RenderTargetTexture, 
	const FString& EventName, 
	TConstArrayView<FRHICopyTextureInfo> CopyInfos,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams,
	bool bClearRenderTarget,
	bool bOrthographicCamera
	)
{
	SceneRenderer->RenderThreadBegin(RHICmdList);

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);

	const ERHIFeatureLevel::Type FeatureLevel = SceneRenderer->FeatureLevel;

#if WANTS_DRAW_MESH_EVENTS
	SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("SceneCapture %s"), EventName);
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SceneCapture %s", *EventName), ERDGBuilderFlags::Parallel);
#else
	SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneCaptureContent_RenderThread);
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SceneCapture"), ERDGBuilderFlags::Parallel);
#endif

	{
		// The target texture is what gets rendered to, while OutputTexture is the final output.  For 2D scene captures, these textures
		// are the same.  For cube captures, OutputTexture will be a cube map, while TargetTexture will be a 2D render target containing either
		// one face of the cube map (when GSceneCaptureCubeSinglePass=0) or the six faces of the cube map tiled in a split screen configuration.
		FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder, RenderTarget->GetRenderTargetTexture(), TEXT("SceneCaptureTarget"));
		FRDGTextureRef OutputTexture = RegisterExternalTexture(GraphBuilder, RenderTargetTexture->TextureRHI, TEXT("SceneCaptureTexture"));

		if (bClearRenderTarget)
		{
			AddClearRenderTargetPass(GraphBuilder, TargetTexture, FLinearColor::Black, SceneRenderer->Views[0].UnscaledViewRect);
		}

		// The lambda below applies to tiled orthographic rendering, where the captured result is blitted from the origin in a scene texture
		// to a viewport on a larger output texture.  It specifically doesn't apply to cube maps, where the output texture has the same tiling
		// as the scene textures, and no viewport remapping is required.
		if (!CopyInfos[0].Size.IsZero() && !OutputTexture->Desc.IsTextureCube())
		{
			// Fix for static analysis warning -- lambda lifetime exceeds lifetime of CopyInfos.  Technically, the lambda is consumed in the
			// scene render call below and not used afterwards, but static analysis doesn't know that, so we make a copy.
			TArray<FRHICopyTextureInfo, TInlineAllocator<1>> CopyInfosLocal(CopyInfos);

			CopyCaptureToTargetSetViewportFn = [CopyInfos = MoveTemp(CopyInfosLocal)](FRHICommandList& RHICmdList, int32 ViewIndex)
			{
				const FIntRect CopyDestRect = CopyInfos[ViewIndex].GetDestRect();

				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
				RHICmdList.SetViewport
				(
					float(CopyDestRect.Min.X),
					float(CopyDestRect.Min.Y),
					0.0f,
					float(CopyDestRect.Max.X),
					float(CopyDestRect.Max.Y),
					1.0f
				);
			};
		}
		else
		{
			CopyCaptureToTargetSetViewportFn = [](FRHICommandList& RHICmdList, int32 ViewIndex) {};
		}


		// Disable occlusion queries when in orthographic mode
		if (bOrthographicCamera)
		{
			FViewInfo& View = SceneRenderer->Views[0];
			View.bDisableQuerySubmissions = true;
			View.bIgnoreExistingQueries = true;
		}

		// Render the scene normally
		{
			RDG_RHI_EVENT_SCOPE(GraphBuilder, RenderScene);
			SceneRenderer->Render(GraphBuilder);
		}

		// These copies become a no-op (function returns immediately) if TargetTexture and OutputTexture are the same, which
		// is true for 2D scene captures.  Actual copies only occur for cube captures, where copying is necessary to get
		// result data to specific slices.
		for (const FRHICopyTextureInfo& CopyInfo : CopyInfos)
		{
			AddCopyTexturePass(GraphBuilder, TargetTexture, OutputTexture, CopyInfo);
		}

		if (bGenerateMips)
		{
			FGenerateMips::Execute(GraphBuilder, SceneRenderer->FeatureLevel, OutputTexture, GenerateMipsParams);
		}

		GraphBuilder.Execute();
	}

	SceneRenderer->RenderThreadEnd(RHICmdList);
}

static void UpdateSceneCaptureContentMobile_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& EventName,
	TConstArrayView<FRHICopyTextureInfo> CopyInfos,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams)
{
	SceneRenderer->RenderThreadBegin(RHICmdList);

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);

#if WANTS_DRAW_MESH_EVENTS
	SCOPED_DRAW_EVENTF(RHICmdList, SceneCaptureMobile, TEXT("SceneCaptureMobile %s"), EventName);
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SceneCaptureMobile %s", *EventName));
#else
	SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneCaptureContentMobile_RenderThread);
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SceneCaptureMobile"));
#endif

	{
		// The target texture is what gets rendered to, while OutputTexture is the final output.  For 2D scene captures, these textures
		// are the same.  For cube captures, OutputTexture will be a cube map, while TargetTexture will be a 2D render target containing either
		// one face of the cube map (when GSceneCaptureCubeSinglePass=0) or the six faces of the cube map tiled in a split screen configuration.
		FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder, RenderTarget->GetRenderTargetTexture(), TEXT("SceneCaptureTarget"));
		FRDGTextureRef OutputTexture = RegisterExternalTexture(GraphBuilder, RenderTargetTexture->TextureRHI, TEXT("SceneCaptureTexture"));

		// The lambda below applies to tiled orthographic rendering, where the captured result is blitted from the origin in a scene texture
		// to a viewport on a larger output texture.  It specifically doesn't apply to cube maps, where the output texture has the same tiling
		// as the scene textures, and no viewport remapping is required.
		if (!CopyInfos[0].Size.IsZero() && !OutputTexture->Desc.IsTextureCube())
		{
			// Fix for static analysis warning -- lambda lifetime exceeds lifetime of CopyInfos.  Technically, the lambda is consumed in the
			// scene render call below and not used afterwards, but static analysis doesn't know that, so we make a copy.
			TArray<FRHICopyTextureInfo, TInlineAllocator<1>> CopyInfosLocal(CopyInfos);

			CopyCaptureToTargetSetViewportFn = [CopyInfos = MoveTemp(CopyInfosLocal)](FRHICommandList& RHICmdList, int32 ViewIndex)
			{
				const FIntRect CopyDestRect = CopyInfos[ViewIndex].GetDestRect();

				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
				RHICmdList.SetViewport
				(
					float(CopyDestRect.Min.X),
					float(CopyDestRect.Min.Y),
					0.0f,
					float(CopyDestRect.Max.X),
					float(CopyDestRect.Max.Y),
					1.0f
				);
			};
		}
		else
		{
			CopyCaptureToTargetSetViewportFn = [](FRHICommandList& RHICmdList, int32 ViewIndex) {};
		}

		// Render the scene normally
		{
			RDG_RHI_EVENT_SCOPE(GraphBuilder, RenderScene);
			SceneRenderer->Render(GraphBuilder);
		}

		{
			// Handles copying the SceneColor render target to the output if necessary (this happens inside the renderer for the deferred path).
			// Other scene captures are automatically written directly to the output, in which case this function returns and does nothing.
			const FRenderTarget* FamilyTarget = SceneRenderer->ViewFamily.RenderTarget;
			FRDGTextureRef FamilyTexture = RegisterExternalTexture(GraphBuilder, FamilyTarget->GetRenderTargetTexture(), TEXT("OutputTexture"));
			const FMinimalSceneTextures& SceneTextures = SceneRenderer->GetActiveSceneTextures();

			RDG_EVENT_SCOPE(GraphBuilder, "CaptureSceneColor");
			CopySceneCaptureComponentToTarget(
				GraphBuilder,
				SceneTextures,
				FamilyTexture,
				SceneRenderer->ViewFamily,
				SceneRenderer->Views);
		}

		// These copies become a no-op (function returns immediately) if TargetTexture and OutputTexture are the same, which
		// is true for 2D scene captures.  Actual copies only occur for cube captures, where copying is necessary to get
		// result data to specific slices.
		for (const FRHICopyTextureInfo& CopyInfo : CopyInfos)
		{
			AddCopyTexturePass(GraphBuilder, TargetTexture, OutputTexture, CopyInfo);
		}

		if (bGenerateMips)
		{
			FGenerateMips::Execute(GraphBuilder, SceneRenderer->FeatureLevel, OutputTexture, GenerateMipsParams);
		}

		GraphBuilder.Execute();
	}

	SceneRenderer->RenderThreadEnd(RHICmdList);
}

static void UpdateSceneCaptureContent_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& EventName,
	TConstArrayView<FRHICopyTextureInfo> CopyInfos,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams,
	bool bClearRenderTarget,
	bool bOrthographicCamera)
{
	FUniformExpressionCacheAsyncUpdateScope AsyncUpdateScope;

	switch (GetFeatureLevelShadingPath(SceneRenderer->Scene->GetFeatureLevel()))
	{
		case EShadingPath::Mobile:
		{
			UpdateSceneCaptureContentMobile_RenderThread(
				RHICmdList,
				SceneRenderer,
				RenderTarget,
				RenderTargetTexture,
				EventName,
				CopyInfos,
				bGenerateMips,
				GenerateMipsParams);
			break;
		}
		case EShadingPath::Deferred:
		{
			UpdateSceneCaptureContentDeferred_RenderThread(
				RHICmdList,
				SceneRenderer,
				RenderTarget,
				RenderTargetTexture,
				EventName,
				CopyInfos,
				bGenerateMips,
				GenerateMipsParams,
				bClearRenderTarget,
				bOrthographicCamera);
			break;
		}
		default:
			checkNoEntry();
			break;
	}

	RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture->TextureRHI, ERHIAccess::Unknown, ERHIAccess::SRVMask));
}

static void BuildOrthoMatrix(FIntPoint InRenderTargetSize, float InOrthoWidth, int32 InTileID, int32 InNumXTiles, int32 InNumYTiles, FMatrix& OutProjectionMatrix)
{
	check((int32)ERHIZBuffer::IsInverted);
	float const XAxisMultiplier = 1.0f;
	float const YAxisMultiplier = InRenderTargetSize.X / float(InRenderTargetSize.Y);

	const float OrthoWidth = InOrthoWidth / 2.0f;
	const float OrthoHeight = InOrthoWidth / 2.0f * XAxisMultiplier / YAxisMultiplier;

	const float NearPlane = 0;
	const float FarPlane = UE_FLOAT_HUGE_DISTANCE / 4.0f;

	const float ZScale = 1.0f / (FarPlane - NearPlane);
	const float ZOffset = -NearPlane;

	if (InTileID == -1)
	{
		OutProjectionMatrix = FReversedZOrthoMatrix(
			OrthoWidth,
			OrthoHeight,
			ZScale,
			ZOffset
		);
		
		return;
	}

#if DO_CHECK
	check(InNumXTiles != 0 && InNumYTiles != 0);
	if (InNumXTiles == 0 || InNumYTiles == 0)
	{
		OutProjectionMatrix = FMatrix(EForceInit::ForceInitToZero);
		return;
	}
#endif

	const float XTileDividerRcp = 1.0f / float(InNumXTiles);
	const float YTileDividerRcp = 1.0f / float(InNumYTiles);

	const float TileX = float(InTileID % InNumXTiles);
	const float TileY = float(InTileID / InNumXTiles);

	float l = -OrthoWidth + TileX * InOrthoWidth * XTileDividerRcp;
	float r = l + InOrthoWidth * XTileDividerRcp;
	float t = OrthoHeight - TileY * InOrthoWidth * YTileDividerRcp;
	float b = t - InOrthoWidth * YTileDividerRcp;

	OutProjectionMatrix = FMatrix(
		FPlane(2.0f / (r-l), 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, 2.0f / (t-b), 0.0f, 0.0f),
		FPlane(0.0f, 0.0f, -ZScale, 0.0f),
		FPlane(-((r+l)/(r-l)), -((t+b)/(t-b)), 1.0f - ZOffset * ZScale, 1.0f)
	);
}

void BuildProjectionMatrix(FIntPoint InRenderTargetSize, float InFOV, float InNearClippingPlane, FMatrix& OutProjectionMatrix)
{
	float const XAxisMultiplier = 1.0f;
	float const YAxisMultiplier = InRenderTargetSize.X / float(InRenderTargetSize.Y);

	if ((int32)ERHIZBuffer::IsInverted)
	{
		OutProjectionMatrix = FReversedZPerspectiveMatrix(
			InFOV,
			InFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			InNearClippingPlane,
			InNearClippingPlane
			);
	}
	else
	{
		OutProjectionMatrix = FPerspectiveMatrix(
			InFOV,
			InFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			InNearClippingPlane,
			InNearClippingPlane
			);
	}
}

void GetShowOnlyAndHiddenComponents(USceneCaptureComponent* SceneCaptureComponent, TSet<FPrimitiveComponentId>& HiddenPrimitives, TOptional<TSet<FPrimitiveComponentId>>& ShowOnlyPrimitives)
{
	check(SceneCaptureComponent);
	for (auto It = SceneCaptureComponent->HiddenComponents.CreateConstIterator(); It; ++It)
	{
		// If the primitive component was destroyed, the weak pointer will return NULL.
		UPrimitiveComponent* PrimitiveComponent = It->Get();
		if (PrimitiveComponent)
		{
			HiddenPrimitives.Add(PrimitiveComponent->GetPrimitiveSceneId());
		}
	}

	for (auto It = SceneCaptureComponent->HiddenActors.CreateConstIterator(); It; ++It)
	{
		AActor* Actor = *It;

		if (Actor)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
				{
					HiddenPrimitives.Add(PrimComp->GetPrimitiveSceneId());
				}
			}
		}
	}

	if (SceneCaptureComponent->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
	{
		ShowOnlyPrimitives.Emplace();

		for (auto It = SceneCaptureComponent->ShowOnlyComponents.CreateConstIterator(); It; ++It)
		{
			// If the primitive component was destroyed, the weak pointer will return NULL.
			UPrimitiveComponent* PrimitiveComponent = It->Get();
			if (PrimitiveComponent)
			{
				ShowOnlyPrimitives->Add(PrimitiveComponent->GetPrimitiveSceneId());
			}
		}

		for (auto It = SceneCaptureComponent->ShowOnlyActors.CreateConstIterator(); It; ++It)
		{
			AActor* Actor = *It;

			if (Actor)
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
					{
						ShowOnlyPrimitives->Add(PrimComp->GetPrimitiveSceneId());
					}
				}
			}
		}
	}
	else if (SceneCaptureComponent->ShowOnlyComponents.Num() > 0 || SceneCaptureComponent->ShowOnlyActors.Num() > 0)
	{
		static bool bWarned = false;

		if (!bWarned)
		{
			UE_LOG(LogRenderer, Log, TEXT("Scene Capture has ShowOnlyComponents or ShowOnlyActors ignored by the PrimitiveRenderMode setting! %s"), *SceneCaptureComponent->GetPathName());
			bWarned = true;
		}
	}
}

TArray<FSceneView*> SetupViewFamilyForSceneCapture(
	FSceneViewFamily& ViewFamily,
	USceneCaptureComponent* SceneCaptureComponent,
	const TArrayView<const FSceneCaptureViewInfo> Views,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	bool bIsPlanarReflection,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor,
	int32 CubemapFaceIndex)
{
	check(!ViewFamily.GetScreenPercentageInterface());

	// For cube map capture, CubeMapFaceIndex takes precedence over view index, so we must have only one view for that case.
	// Or if CubemapFaceIndex == CubeFace_MAX (6), it's a renderer for all 6 cube map faces.
	check(CubemapFaceIndex == INDEX_NONE || Views.Num() == 1 || (CubemapFaceIndex == CubeFace_MAX && Views.Num() == CubeFace_MAX));

	// Initialize frame number
	ViewFamily.FrameNumber = ViewFamily.Scene->GetFrameNumber();
	ViewFamily.FrameCounter = GFrameCounter;

	TArray<FSceneView*> ViewPtrArray;
	ViewPtrArray.Reserve(Views.Num());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FSceneCaptureViewInfo& SceneCaptureViewInfo = Views[ViewIndex];

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(SceneCaptureViewInfo.ViewRect);
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewActor = ViewActor;
		ViewInitOptions.ViewLocation = SceneCaptureViewInfo.ViewLocation;
		ViewInitOptions.ViewRotation = SceneCaptureViewInfo.ViewRotation;
		ViewInitOptions.ViewOrigin = SceneCaptureViewInfo.ViewOrigin;
		ViewInitOptions.ViewRotationMatrix = SceneCaptureViewInfo.ViewRotationMatrix;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverrideFarClippingPlaneDistance = MaxViewDistance;
		ViewInitOptions.StereoPass = SceneCaptureViewInfo.StereoPass;
		ViewInitOptions.StereoViewIndex = SceneCaptureViewInfo.StereoViewIndex;
		ViewInitOptions.ProjectionMatrix = SceneCaptureViewInfo.ProjectionMatrix;
		ViewInitOptions.bIsSceneCapture = true;
		ViewInitOptions.bIsPlanarReflection = bIsPlanarReflection;
		ViewInitOptions.FOV = SceneCaptureViewInfo.FOV;
		ViewInitOptions.DesiredFOV = SceneCaptureViewInfo.FOV;

		if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}

		if (bCaptureSceneColor)
		{
			ViewFamily.EngineShowFlags.PostProcessing = 0;
			ViewInitOptions.OverlayColor = FLinearColor::Black;
		}

		if (SceneCaptureComponent)
		{
			// Use CubemapFaceIndex if in range [0..CubeFace_MAX), otherwise use ViewIndex.  Casting to unsigned treats -1 as a large value, choosing ViewIndex.
			ViewInitOptions.SceneViewStateInterface = SceneCaptureComponent->GetViewState((uint32)CubemapFaceIndex < CubeFace_MAX ? CubemapFaceIndex : ViewIndex);
			ViewInitOptions.LODDistanceFactor = FMath::Clamp(SceneCaptureComponent->LODDistanceFactor, .01f, 100.0f);
			ViewInitOptions.bIsSceneCaptureCube = SceneCaptureComponent->IsCube();
			ViewInitOptions.bSceneCaptureUsesRayTracing = GRayTracingSceneCaptures == -1 ? SceneCaptureComponent->bUseRayTracingIfEnabled : GRayTracingSceneCaptures > 0;
		}

		FSceneView* View = new FSceneView(ViewInitOptions);
		
		if (SceneCaptureComponent)
		{
			GetShowOnlyAndHiddenComponents(SceneCaptureComponent, View->HiddenPrimitives, View->ShowOnlyPrimitives);
		}
		
		ViewFamily.Views.Add(View);
		ViewPtrArray.Add(View);

		View->StartFinalPostprocessSettings(SceneCaptureViewInfo.ViewOrigin);

		// By default, Lumen is disabled in scene captures, but can be re-enabled with the post process settings in the component.
		View->FinalPostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
		View->FinalPostProcessSettings.ReflectionMethod = EReflectionMethod::None;

		// Default surface cache to lower resolution for Scene Capture.  Can be overridden via post process settings.
		View->FinalPostProcessSettings.LumenSurfaceCacheResolution = 0.5f;

		if (SceneCaptureComponent && SceneCaptureComponent->IsCube())
		{
			// Disable vignette by default for cube maps -- darkened borders don't make sense for an omnidirectional projection.
			View->FinalPostProcessSettings.VignetteIntensity = 0.0f;

			// Disable screen traces by default for cube maps -- these don't blend well across face boundaries, creating major lighting seams.
			// Lumen lighting still has some seams with these disabled, but it's an order of magnitude better.
			View->FinalPostProcessSettings.LumenReflectionsScreenTraces = 0;
			View->FinalPostProcessSettings.LumenFinalGatherScreenTraces = 0;
		}

		if (PostProcessSettings)
		{
			View->OverridePostProcessSettings(*PostProcessSettings, PostProcessBlendWeight);
		}
		View->EndFinalPostprocessSettings(ViewInitOptions);
	}

	return ViewPtrArray;
}

void SetupSceneViewExtensionsForSceneCapture(
	FSceneViewFamily& ViewFamily,
	TConstArrayView<FSceneView*> Views)
{
	for (const FSceneViewExtensionRef& Extension : ViewFamily.ViewExtensions)
	{
		Extension->SetupViewFamily(ViewFamily);
	}

	for (FSceneView* View : Views)
	{
		for (const FSceneViewExtensionRef& Extension : ViewFamily.ViewExtensions)
		{
			Extension->SetupView(ViewFamily, *View);
		}
	}
}

static FSceneRenderer* CreateSceneRendererForSceneCapture(
	FScene* Scene,
	USceneCaptureComponent* SceneCaptureComponent,
	FRenderTarget* RenderTarget,
	FIntPoint RenderTargetSize,
	const FMatrix& ViewRotationMatrix,
	const FVector& ViewLocation,
	const FMatrix& ProjectionMatrix,
	float MaxViewDistance,
	float InFOV,
	bool bCaptureSceneColor,
	bool bCameraCut2D,
	bool bCopyMainViewTemporalSettings2D,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor, 
	int32 CubemapFaceIndex = INDEX_NONE)
{
	FSceneCaptureViewInfo SceneCaptureViewInfo;
	SceneCaptureViewInfo.ViewRotationMatrix = ViewRotationMatrix;
	SceneCaptureViewInfo.ViewOrigin = ViewLocation;
	SceneCaptureViewInfo.ProjectionMatrix = ProjectionMatrix;
	SceneCaptureViewInfo.StereoPass = EStereoscopicPass::eSSP_FULL;
	SceneCaptureViewInfo.StereoViewIndex = INDEX_NONE;
	SceneCaptureViewInfo.ViewRect = FIntRect(0, 0, RenderTargetSize.X, RenderTargetSize.Y);
	SceneCaptureViewInfo.FOV = InFOV;

	bool bInheritMainViewScreenPercentage = false;
	USceneCaptureComponent2D* SceneCaptureComponent2D = Cast<USceneCaptureComponent2D>(SceneCaptureComponent);

	// Use camera position correction for ortho scene captures
	if(IsValid(SceneCaptureComponent2D))
	{
		if (!SceneCaptureViewInfo.IsPerspectiveProjection() && SceneCaptureComponent2D->bUpdateOrthoPlanes)
		{
			SceneCaptureViewInfo.UpdateOrthoPlanes(SceneCaptureComponent2D->bUseCameraHeightAsViewTarget);
		}

		if (SceneCaptureComponent2D->ShouldRenderWithMainViewResolution() && SceneCaptureComponent2D->MainViewFamily && !SceneCaptureComponent2D->ShouldIgnoreScreenPercentage())
		{
			bInheritMainViewScreenPercentage = true;
		}
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		Scene,
		SceneCaptureComponent->ShowFlags)
		.SetResolveScene(!bCaptureSceneColor)
		.SetRealtimeUpdate(SceneCaptureComponent->bCaptureEveryFrame || SceneCaptureComponent->bAlwaysPersistRenderingState));

	FSceneViewExtensionContext ViewExtensionContext(Scene);
	ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);
	
	TArray<FSceneView*> Views = SetupViewFamilyForSceneCapture(
		ViewFamily,
		SceneCaptureComponent,
		MakeArrayView(&SceneCaptureViewInfo, 1),
		MaxViewDistance, 
		bCaptureSceneColor,
		/* bIsPlanarReflection = */ false,
		PostProcessSettings, 
		PostProcessBlendWeight,
		ViewActor,
		CubemapFaceIndex);

	// Scene capture source is used to determine whether to disable occlusion queries inside FSceneRenderer constructor
	ViewFamily.SceneCaptureSource = SceneCaptureComponent->CaptureSource;

	if (bInheritMainViewScreenPercentage)
	{
		ViewFamily.EngineShowFlags.ScreenPercentage = SceneCaptureComponent2D->MainViewFamily->EngineShowFlags.ScreenPercentage;
		ViewFamily.SetScreenPercentageInterface(SceneCaptureComponent2D->MainViewFamily->GetScreenPercentageInterface()->Fork_GameThread(ViewFamily));
	}
	else
	{
		// Screen percentage is still not supported in scene capture.
		ViewFamily.EngineShowFlags.ScreenPercentage = false;
		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
			ViewFamily, /* GlobalResolutionFraction = */ 1.0f));
	}

	if (IsValid(SceneCaptureComponent2D))
	{
		// Scene capture 2D only support a single view
		check(Views.Num() == 1);

		// Ensure that the views for this scene capture reflect any simulated camera motion for this frame
		TOptional<FTransform> PreviousTransform = FMotionVectorSimulation::Get().GetPreviousTransform(SceneCaptureComponent2D);

		// Update views with scene capture 2d specific settings
		if (PreviousTransform.IsSet())
		{
			Views[0]->PreviousViewTransform = PreviousTransform.GetValue();
		}

		if (SceneCaptureComponent2D->bEnableClipPlane)
		{
			Views[0]->GlobalClippingPlane = FPlane(SceneCaptureComponent2D->ClipPlaneBase, SceneCaptureComponent2D->ClipPlaneNormal.GetSafeNormal());
			// Jitter can't be removed completely due to the clipping plane
			Views[0]->bAllowTemporalJitter = false;
		}

		Views[0]->bCameraCut = bCameraCut2D;

		if (bCopyMainViewTemporalSettings2D)
		{
			const FSceneViewFamily* MainViewFamily = SceneCaptureComponent2D->MainViewFamily;
			const FSceneView& SourceView = *MainViewFamily->Views[0];

			Views[0]->AntiAliasingMethod = SourceView.AntiAliasingMethod;
			Views[0]->PrimaryScreenPercentageMethod = SourceView.PrimaryScreenPercentageMethod;

			if (Views[0]->State && SourceView.State)
			{
				((FSceneViewState*)Views[0]->State)->TemporalAASampleIndex = ((const FSceneViewState*)SourceView.State)->TemporalAASampleIndex;
			}
		}

		// Append component-local view extensions to the view family
		for (int32 Index = 0; Index < SceneCaptureComponent2D->SceneViewExtensions.Num(); ++Index)
		{
			TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> Extension = SceneCaptureComponent2D->SceneViewExtensions[Index].Pin();
			if (Extension.IsValid())
			{
				if (Extension->IsActiveThisFrame(ViewExtensionContext))
				{
					ViewFamily.ViewExtensions.Add(Extension.ToSharedRef());
				}
			}
			else
			{
				SceneCaptureComponent2D->SceneViewExtensions.RemoveAt(Index, EAllowShrinking::No);
				--Index;
			}
		}
	}

	// Call SetupViewFamily & SetupView on scene view extensions before renderer creation
	SetupSceneViewExtensionsForSceneCapture(ViewFamily, Views);

	return FSceneRenderer::CreateSceneRenderer(&ViewFamily, nullptr);
}

FSceneCaptureCustomRenderPassUserData FSceneCaptureCustomRenderPassUserData::GDefaultData;

class FSceneCapturePass final : public FCustomRenderPassBase
{
public:
	IMPLEMENT_CUSTOM_RENDER_PASS(FSceneCapturePass);

	FSceneCapturePass(const FString& InDebugName, ERenderMode InRenderMode, ERenderOutput InRenderOutput, UTextureRenderTarget2D* InRenderTarget, USceneCaptureComponent2D* CaptureComponent, FIntPoint InRenderTargetSize)
		: FCustomRenderPassBase(InDebugName, InRenderMode, InRenderOutput, InRenderTargetSize)
		, SceneCaptureRenderTarget(InRenderTarget->GameThread_GetRenderTargetResource())
		, bAutoGenerateMips(InRenderTarget->bAutoGenerateMips)
	{
		FSceneCaptureCustomRenderPassUserData* UserData = new FSceneCaptureCustomRenderPassUserData();
		UserData->bMainViewFamily = CaptureComponent->ShouldRenderWithMainViewFamily();
		UserData->bMainViewResolution = CaptureComponent->ShouldRenderWithMainViewResolution();
		UserData->bMainViewCamera = CaptureComponent->ShouldRenderWithMainViewCamera();
		UserData->bIgnoreScreenPercentage = CaptureComponent->ShouldIgnoreScreenPercentage();
		UserData->SceneTextureDivisor = CaptureComponent->MainViewResolutionDivisor.ComponentMax(FIntPoint(1,1));
		UserData->UserSceneTextureBaseColor = CaptureComponent->UserSceneTextureBaseColor;
		UserData->UserSceneTextureNormal = CaptureComponent->UserSceneTextureNormal;
		UserData->UserSceneTextureSceneColor = CaptureComponent->UserSceneTextureSceneColor;
#if !UE_BUILD_SHIPPING
		CaptureComponent->GetOuter()->GetName(UserData->CaptureActorName);
#endif

		SetUserData(TUniquePtr<FSceneCaptureCustomRenderPassUserData>(UserData));
	}

	virtual void OnPreRender(FRDGBuilder& GraphBuilder) override
	{
		// Resize the render resource if necessary -- render target size may have been overridden to the main view resolution, or later be changed back
		// to the resource resolution.  The resize call does nothing if the size already matches.
		((FTextureRenderTarget2DResource*)SceneCaptureRenderTarget)->Resize(GraphBuilder.RHICmdList, RenderTargetSize.X, RenderTargetSize.Y, bAutoGenerateMips);

		RenderTargetTexture = SceneCaptureRenderTarget->GetRenderTargetTexture(GraphBuilder);
	}

	virtual void OnEndPass(FRDGBuilder& GraphBuilder) override
	{
		// Materials in the main view renderer will be using this render target, so we need RDG to transition it back to SRV now,
		// rather than at the end of graph execution.
		GraphBuilder.UseExternalAccessMode(RenderTargetTexture, ERHIAccess::SRVMask);
	}
	
	FRenderTarget* SceneCaptureRenderTarget = nullptr;
	bool bAutoGenerateMips = false;
};

static void BeginGpuCaptureOrDump(USceneCaptureComponent* CaptureComponent, bool& bCapturingGPU, bool& bDumpingGPU)
{
	bCapturingGPU = false;
	bDumpingGPU = false;

	if (CaptureComponent->bSuppressGpuCaptureOrDump)
	{
		CaptureComponent->bSuppressGpuCaptureOrDump = false;
		return;
	}
	
	bCapturingGPU = CaptureComponent->bCaptureGpuNextRender;
	bDumpingGPU = CaptureComponent->bDumpGpuNextRender;

	CaptureComponent->bCaptureGpuNextRender = false;
	CaptureComponent->bDumpGpuNextRender = false;

	// Clear capturing flag if it's not available
	if (!IRenderCaptureProvider::IsAvailable())
	{
		bCapturingGPU = false;
	}

	// If user sets both capture and dump flags, prefer capturing over dumping (or clear flag if dumping is not available)
	if (bCapturingGPU || !(WITH_ENGINE && WITH_DUMPGPU))
	{
		bDumpingGPU = false;
	}

#if WITH_ENGINE && WITH_DUMPGPU
	if (bDumpingGPU)
	{
		// Don't try to start a dump if we are already dumping for some reason
		if (FRDGBuilder::IsDumpingFrame())
		{
			bDumpingGPU = false;
		}
		else
		{
			// Pass "-oneframe" to override CVar that could enable multiple frames of capture
			FRDGBuilder::BeginResourceDump(TEXT("-oneframe"));

			// Tick the DumpGPU system, which will start the dump
			UE::RenderCore::DumpGPU::TickEndFrame();
		}
	}
#endif
}

static void EndGpuCaptureOrDump(bool bDumpingGPU)
{
#if WITH_ENGINE && WITH_DUMPGPU
	if (bDumpingGPU)
	{
		// Tick the dump GPU system again, which will end the active dump, so it just includes the scene capture
		UE::RenderCore::DumpGPU::TickEndFrame();
	}
#endif
}

void FScene::UpdateSceneCaptureContents(USceneCaptureComponent2D* CaptureComponent)
{
	check(CaptureComponent);

	bool bCapturingGPU;
	bool bDumpingGPU;
	BeginGpuCaptureOrDump(CaptureComponent, bCapturingGPU, bDumpingGPU);

	if (UTextureRenderTarget2D* TextureRenderTarget = CaptureComponent->TextureTarget)
	{
		FIntPoint CaptureSize;
		FVector ViewLocation;
		FMatrix ViewRotationMatrix;
		FMatrix ProjectionMatrix;
		bool bEnableOrthographicTiling;

		const bool bUseSceneColorTexture = CaptureNeedsSceneColor(CaptureComponent->CaptureSource);

		const int32 TileID = CaptureComponent->TileID;
		const int32 NumXTiles = CaptureComponent->GetNumXTiles();
		const int32 NumYTiles = CaptureComponent->GetNumYTiles();

		if (CaptureComponent->ShouldRenderWithMainViewResolution() && CaptureComponent->MainViewFamily)
		{
			CaptureSize = CaptureComponent->MainViewFamily->Views[0]->UnscaledViewRect.Size();
			CaptureSize = FIntPoint::DivideAndRoundUp(CaptureSize, CaptureComponent->MainViewResolutionDivisor.ComponentMax(FIntPoint(1,1)));

			// Main view resolution rendering doesn't support orthographic tiling
			bEnableOrthographicTiling = false;
		}
		else
		{
			CaptureSize = FIntPoint(TextureRenderTarget->GetSurfaceWidth(), TextureRenderTarget->GetSurfaceHeight());

			bEnableOrthographicTiling = (CaptureComponent->GetEnableOrthographicTiling() && CaptureComponent->ProjectionType == ECameraProjectionMode::Orthographic && bUseSceneColorTexture);

			if (CaptureComponent->GetEnableOrthographicTiling() && CaptureComponent->ProjectionType == ECameraProjectionMode::Orthographic && !bUseSceneColorTexture)
			{
				UE_LOG(LogRenderer, Warning, TEXT("SceneCapture - Orthographic and tiling with CaptureSource not using SceneColor (i.e FinalColor) not compatible. SceneCapture render will not be tiled"));
			}
		}

		if (CaptureComponent->ShouldRenderWithMainViewCamera() && CaptureComponent->MainViewFamily)
		{
			const FSceneView* MainView = CaptureComponent->MainViewFamily->Views[0];

			ViewLocation = MainView->ViewMatrices.GetViewOrigin();
			ViewRotationMatrix = MainView->ViewMatrices.GetViewMatrix().RemoveTranslation();
			ProjectionMatrix = MainView->ViewMatrices.GetProjectionMatrix();
		}
		else
		{
			FTransform Transform = CaptureComponent->GetComponentToWorld();
			ViewLocation = Transform.GetTranslation();

			// Remove the translation from Transform because we only need rotation.
			Transform.SetTranslation(FVector::ZeroVector);
			Transform.SetScale3D(FVector::OneVector);
			ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

			// swap axis st. x=z,y=x,z=y (unreal coord space) so that z is up
			ViewRotationMatrix = ViewRotationMatrix * FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));
			const float UnscaledFOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
			const float FOV = FMath::Atan((1.0f + CaptureComponent->Overscan) * FMath::Tan(UnscaledFOV));

			if (CaptureComponent->bUseCustomProjectionMatrix)
			{
				ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
			}
			else
			{
				if (CaptureComponent->ProjectionType == ECameraProjectionMode::Perspective)
				{
					const float ClippingPlane = (CaptureComponent->bOverride_CustomNearClippingPlane) ? CaptureComponent->CustomNearClippingPlane : GNearClippingPlane;
					BuildProjectionMatrix(CaptureSize, FOV, ClippingPlane, ProjectionMatrix);
				}
				else
				{
					if (bEnableOrthographicTiling)
					{
						BuildOrthoMatrix(CaptureSize, CaptureComponent->OrthoWidth, CaptureComponent->TileID, NumXTiles, NumYTiles, ProjectionMatrix);
						CaptureSize /= FIntPoint(NumXTiles, NumYTiles);
					}
					else
					{
						BuildOrthoMatrix(CaptureSize, CaptureComponent->OrthoWidth, -1, 0, 0, ProjectionMatrix);
					}
				}
			}
		}

		// As optimization for depth capture modes, render scene capture as additional render passes inside the main renderer.
		if (GSceneCaptureAllowRenderInMainRenderer && 
			CaptureComponent->bRenderInMainRenderer && 
			(CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_SceneDepth || CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_DeviceDepth ||
			 CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_BaseColor || CaptureComponent->CaptureSource == ESceneCaptureSource::SCS_Normal)
			)
		{
			FCustomRenderPassRendererInput PassInput;
			PassInput.ViewLocation = ViewLocation;
			PassInput.ViewRotationMatrix = ViewRotationMatrix;
			PassInput.ProjectionMatrix = ProjectionMatrix;
			PassInput.ViewActor = CaptureComponent->GetViewOwner();
			PassInput.bIsSceneCapture = true;

			FCustomRenderPassBase::ERenderMode RenderMode;
			FCustomRenderPassBase::ERenderOutput RenderOutput;
			const TCHAR* DebugName;

			bool bHasUserSceneTextureOutput = !CaptureComponent->UserSceneTextureBaseColor.IsNone() || !CaptureComponent->UserSceneTextureNormal.IsNone() || !CaptureComponent->UserSceneTextureSceneColor.IsNone();

			switch (CaptureComponent->CaptureSource)
			{
			case ESceneCaptureSource::SCS_SceneDepth:
				if (bHasUserSceneTextureOutput)
				{
					// If a UserSceneTexture output is specified, the base pass needs to run to generate it.
					RenderMode = FCustomRenderPassBase::ERenderMode::DepthAndBasePass;
				}
				else
				{
					RenderMode = FCustomRenderPassBase::ERenderMode::DepthPass;
				}
				RenderOutput = FCustomRenderPassBase::ERenderOutput::SceneDepth;
				DebugName = TEXT("SceneCapturePass_SceneDepth");
				break;
			case ESceneCaptureSource::SCS_DeviceDepth:
				if (bHasUserSceneTextureOutput)
				{
					RenderMode = FCustomRenderPassBase::ERenderMode::DepthAndBasePass;
				}
				else
				{
					RenderMode = FCustomRenderPassBase::ERenderMode::DepthPass;
				}
				RenderOutput = FCustomRenderPassBase::ERenderOutput::DeviceDepth;
				DebugName = TEXT("SceneCapturePass_DeviceDepth");
				break;
			case ESceneCaptureSource::SCS_Normal:
				RenderMode = FCustomRenderPassBase::ERenderMode::DepthAndBasePass;
				RenderOutput = FCustomRenderPassBase::ERenderOutput::Normal;
				DebugName = TEXT("SceneCapturePass_Normal");
				break;
			case ESceneCaptureSource::SCS_BaseColor:
			default:
				RenderMode = FCustomRenderPassBase::ERenderMode::DepthAndBasePass;
				RenderOutput = FCustomRenderPassBase::ERenderOutput::BaseColor;
				DebugName = TEXT("SceneCapturePass_BaseColor");
				break;
			}

			FSceneCapturePass* CustomPass = new FSceneCapturePass(DebugName, RenderMode, RenderOutput, TextureRenderTarget, CaptureComponent, CaptureSize);
			PassInput.CustomRenderPass = CustomPass;

			GetShowOnlyAndHiddenComponents(CaptureComponent, PassInput.HiddenPrimitives, PassInput.ShowOnlyPrimitives);

			PassInput.EngineShowFlags = CaptureComponent->ShowFlags;

			if (CaptureComponent->PostProcessBlendWeight > 0.0f && CaptureComponent->PostProcessSettings.bOverride_UserFlags)
			{
				PassInput.PostVolumeUserFlags = CaptureComponent->PostProcessSettings.UserFlags;
				PassInput.bOverridesPostVolumeUserFlags = true;
			}

			// Caching scene capture info to be passed to the scene renderer.
			// #todo: We cannot (yet) guarantee for which ViewFamily this CRP will eventually be rendered since it will just execute the next time the scene is rendered by any FSceneRenderer. This seems quite problematic and could easily lead to unexpected behavior...
			AddCustomRenderPass(nullptr, PassInput);
			return;
		}

		
		// Copy temporal AA related settings for main view camera scene capture, to match jitter.  Don't match if the resolution divisor is set,
		// if it's set to ignore screen percentage, or if it's final color, which will run its own AA.  For custom render passes (handled above),
		// computed jitter results are copied from the main view later in FSceneRenderer::PrepareViewStateForVisibility, but this doesn't work
		// for regular scene captures, because they run in a separate scene renderer before the main view, where the main view's results haven't
		// been computed yet.
		const bool bCopyMainViewTemporalSettings2D = (CaptureComponent->ShouldRenderWithMainViewCamera() && CaptureComponent->MainViewFamily &&
			CaptureComponent->MainViewResolutionDivisor.X <= 1 && CaptureComponent->MainViewResolutionDivisor.Y <= 1 && !CaptureComponent->ShouldIgnoreScreenPercentage() &&
			CaptureComponent->CaptureSource != ESceneCaptureSource::SCS_FinalColorLDR &&
			CaptureComponent->CaptureSource != ESceneCaptureSource::SCS_FinalColorHDR &&
			CaptureComponent->CaptureSource != ESceneCaptureSource::SCS_FinalToneCurveHDR);
		const bool bCameraCut2D = bCopyMainViewTemporalSettings2D ? CaptureComponent->MainViewFamily->Views[0]->bCameraCut : CaptureComponent->bCameraCutThisFrame;

		FSceneRenderer* SceneRenderer = CreateSceneRendererForSceneCapture(
			this, 
			CaptureComponent, 
			TextureRenderTarget->GameThread_GetRenderTargetResource(), 
			CaptureSize, 
			ViewRotationMatrix, 
			ViewLocation, 
			ProjectionMatrix, 
			CaptureComponent->MaxViewDistanceOverride, 
			CaptureComponent->FOVAngle,
			bUseSceneColorTexture,
			bCameraCut2D,
			bCopyMainViewTemporalSettings2D,
			&CaptureComponent->PostProcessSettings, 
			CaptureComponent->PostProcessBlendWeight,
			CaptureComponent->GetViewOwner());

		check(SceneRenderer != nullptr);

		// When bIsMultipleSceneCapture is true, set bIsFirstSceneRenderer to false, which tells the scene renderer it can skip RHI resource flush, saving performance
		bool bIsMultipleSceneCapture = CaptureComponent->SetFrameUpdated();
		SceneRenderer->bIsFirstSceneRenderer = !bIsMultipleSceneCapture;

		SceneRenderer->Views[0].bSceneCaptureMainViewJitter = bCopyMainViewTemporalSettings2D;
		SceneRenderer->Views[0].bFogOnlyOnRenderedOpaque = CaptureComponent->bConsiderUnrenderedOpaquePixelAsFullyTranslucent;

		SceneRenderer->ViewFamily.SceneCaptureCompositeMode = CaptureComponent->CompositeMode;

		// Need view state interface to be allocated for Lumen, as it requires persistent data.  This means
		// "bCaptureEveryFrame" or "bAlwaysPersistRenderingState" must be enabled.
		FSceneViewStateInterface* ViewStateInterface = CaptureComponent->GetViewState(0);

		if (ViewStateInterface &&
			(SceneRenderer->Views[0].FinalPostProcessSettings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::Lumen ||
			 SceneRenderer->Views[0].FinalPostProcessSettings.ReflectionMethod == EReflectionMethod::Lumen))
		{
			// It's OK to call these every frame -- they are no-ops if the correct data is already there
			ViewStateInterface->AddLumenSceneData(this, SceneRenderer->Views[0].FinalPostProcessSettings.LumenSurfaceCacheResolution);
		}
		else if (ViewStateInterface)
		{
			ViewStateInterface->RemoveLumenSceneData(this);
		}

		// Reset scene capture's camera cut.
		CaptureComponent->bCameraCutThisFrame = false;

		FTextureRenderTargetResource* TextureRenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();

		FString EventName;
		if (!CaptureComponent->ProfilingEventName.IsEmpty())
		{
			EventName = CaptureComponent->ProfilingEventName;
		}
		else if (CaptureComponent->GetOwner())
		{
			// The label might be non-unique, so include the actor name as well
			EventName = CaptureComponent->GetOwner()->GetActorNameOrLabel();

			FName ActorName = CaptureComponent->GetOwner()->GetFName();
			if (ActorName != EventName)
			{
				EventName.Appendf(TEXT(" (%s)"), *ActorName.ToString());
			}
		}
		FName TargetName = TextureRenderTarget->GetFName();

		const bool bGenerateMips = TextureRenderTarget->bAutoGenerateMips;
		FGenerateMipsParams GenerateMipsParams{TextureRenderTarget->MipsSamplerFilter == TF_Nearest ? SF_Point : (TextureRenderTarget->MipsSamplerFilter == TF_Trilinear ? SF_Trilinear : SF_Bilinear),
			TextureRenderTarget->MipsAddressU == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressU == TA_Mirror ? AM_Mirror : AM_Clamp),
			TextureRenderTarget->MipsAddressV == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressV == TA_Mirror ? AM_Mirror : AM_Clamp)};

		const bool bOrthographicCamera = CaptureComponent->ProjectionType == ECameraProjectionMode::Orthographic;


		// If capturing every frame, only render to the GPUs that are actually being used
		// this frame. We can only determine this by querying the viewport back buffer on
		// the render thread, so pass that along if it exists.
		FRenderTarget* GameViewportRT = nullptr;
		if (CaptureComponent->bCaptureEveryFrame)
		{
			if (GEngine->GameViewport != nullptr)
			{
				GameViewportRT = GEngine->GameViewport->Viewport;
			}
		}

		UTexture* TexturePtrNotDeferenced = TextureRenderTarget;

		// Compositing feature is only active when using SceneColor as the source
		bool bIsCompositing = (CaptureComponent->CompositeMode != SCCM_Overwrite) && (CaptureComponent->CaptureSource == SCS_SceneColorHDR);
#if WITH_EDITOR
		if (!CaptureComponent->CaptureMemorySize)
		{
			CaptureComponent->CaptureMemorySize = new FSceneCaptureMemorySize;
		}
		TRefCountPtr<FSceneCaptureMemorySize> CaptureMemorySize = CaptureComponent->CaptureMemorySize;
#else
		void* CaptureMemorySize = nullptr;		// Dummy value for lambda capture argument list
#endif

		for (const FSceneViewExtensionRef& Extension : SceneRenderer->ViewFamily.ViewExtensions)
		{
			Extension->BeginRenderViewFamily(SceneRenderer->ViewFamily);
		}

		UE::RenderCommandPipe::FSyncScope SyncScope;

		ENQUEUE_RENDER_COMMAND(CaptureCommand)(
			[SceneRenderer, TextureRenderTargetResource, TexturePtrNotDeferenced, EventName, TargetName, bGenerateMips, GenerateMipsParams, GameViewportRT, bEnableOrthographicTiling, bIsCompositing, bOrthographicCamera, NumXTiles, NumYTiles, TileID, CaptureMemorySize, bCapturingGPU, CaptureSize](FRHICommandListImmediate& RHICmdList)
			{
				// Resize the render resource if necessary, either to the main viewport size overridden above (see ShouldRenderWithMainViewResolution()),
				// or the original size if we are changing back to that (the resize call does nothing if the size already matches).
				TextureRenderTargetResource->GetTextureRenderTarget2DResource()->Resize(RHICmdList, CaptureSize.X, CaptureSize.Y, bGenerateMips);

				RenderCaptureInterface::FScopedCapture RenderCapture(bCapturingGPU, &RHICmdList, *FString::Format(TEXT("Scene Capture : {0}"), { EventName }));

				if (GameViewportRT != nullptr)
				{
					const FRHIGPUMask GPUMask = GameViewportRT->GetGPUMask(RHICmdList);
					TextureRenderTargetResource->SetActiveGPUMask(GPUMask);
				}
				else
				{
					TextureRenderTargetResource->SetActiveGPUMask(FRHIGPUMask::All());
				}

				FRHICopyTextureInfo CopyInfo;

				if (bEnableOrthographicTiling)
				{
					const uint32 RTSizeX = TextureRenderTargetResource->GetSizeX() / NumXTiles;
					const uint32 RTSizeY = TextureRenderTargetResource->GetSizeY() / NumYTiles;
					const uint32 TileX = TileID % NumXTiles;
					const uint32 TileY = TileID / NumXTiles;
					CopyInfo.DestPosition.X = TileX * RTSizeX;
					CopyInfo.DestPosition.Y = TileY * RTSizeY;
					CopyInfo.Size.X = RTSizeX;
					CopyInfo.Size.Y = RTSizeY;
				}

				RectLightAtlas::FAtlasTextureInvalidationScope Invalidation(TexturePtrNotDeferenced);

#if WITH_EDITOR
				// Scene renderer may be deleted in UpdateSceneCaptureContent_RenderThread, grab view state pointer first
				const FSceneViewState* ViewState = SceneRenderer->Views[0].ViewState;
#endif  // WITH_EDITOR

				// Don't clear the render target when compositing, or in a tiling mode that fills in the render target in multiple passes.
				bool bClearRenderTarget = !bIsCompositing && !bEnableOrthographicTiling;

				VISUALIZE_TEXTURE_BEGIN_VIEW(SceneRenderer->FeatureLevel, SceneRenderer->Views[0].GetViewKey(), *EventName, true);

				UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, TextureRenderTargetResource, TextureRenderTargetResource, EventName,
					TConstArrayView<FRHICopyTextureInfo>(&CopyInfo, 1), bGenerateMips, GenerateMipsParams, bClearRenderTarget, bOrthographicCamera);

				VISUALIZE_TEXTURE_END_VIEW();

#if WITH_EDITOR
				if (ViewState)
				{
					const bool bLogSizes = GDumpSceneCaptureMemoryFrame == GFrameNumberRenderThread;
					if (bLogSizes)
					{
						UE_LOG(LogRenderer, Log, TEXT("LogSizes\tSceneCapture\t%s\t%s\t%dx%d"), *EventName, *TargetName.ToString(), TextureRenderTargetResource->GetSizeX(), TextureRenderTargetResource->GetSizeY());
					}
					CaptureMemorySize->Size = ViewState->GetGPUSizeBytes(bLogSizes);
				}
				else
				{
					CaptureMemorySize->Size = 0;
				}
#endif  // WITH_EDITOR
			}
		);
	}

	EndGpuCaptureOrDump(bDumpingGPU);
}

// Split screen cube map faces are rendered as 3x2 tiles.
static const int32 GCubeFaceViewportOffsets[6][2] =
{
	{ 0,0 },
	{ 1,0 },
	{ 2,0 },
	{ 0,1 },
	{ 1,1 },
	{ 2,1 },
};

void FScene::UpdateSceneCaptureContents(USceneCaptureComponentCube* CaptureComponent)
{
	bool bCapturingGPU;
	bool bDumpingGPU;
	BeginGpuCaptureOrDump(CaptureComponent, bCapturingGPU, bDumpingGPU);

	struct FLocal
	{
		/** Creates a transformation for a cubemap face, following the D3D cubemap layout. */
		static FMatrix CalcCubeFaceTransform(ECubeFace Face)
		{
			static const FVector XAxis(1.f, 0.f, 0.f);
			static const FVector YAxis(0.f, 1.f, 0.f);
			static const FVector ZAxis(0.f, 0.f, 1.f);

			// vectors we will need for our basis
			FVector vUp(YAxis);
			FVector vDir;
			switch (Face)
			{
				case CubeFace_PosX:
					vDir = XAxis;
					break;
				case CubeFace_NegX:
					vDir = -XAxis;
					break;
				case CubeFace_PosY:
					vUp = -ZAxis;
					vDir = YAxis;
					break;
				case CubeFace_NegY:
					vUp = ZAxis;
					vDir = -YAxis;
					break;
				case CubeFace_PosZ:
					vDir = ZAxis;
					break;
				case CubeFace_NegZ:
					vDir = -ZAxis;
					break;
			}
			// derive right vector
			FVector vRight(vUp ^ vDir);
			// create matrix from the 3 axes
			return FBasisVectorMatrix(vRight, vUp, vDir, FVector::ZeroVector);
		}
	} ;

	check(CaptureComponent);

	FTransform Transform = CaptureComponent->GetComponentToWorld();
	const FVector ViewLocation = Transform.GetTranslation();

	if (CaptureComponent->bCaptureRotation)
	{
		// Remove the translation from Transform because we only need rotation.
		Transform.SetTranslation(FVector::ZeroVector);
		Transform.SetScale3D(FVector::OneVector);
	}

	UTextureRenderTargetCube* const TextureTarget = CaptureComponent->TextureTarget;

	if (TextureTarget)
	{
		FTextureRenderTargetCubeResource* TextureRenderTarget = static_cast<FTextureRenderTargetCubeResource*>(TextureTarget->GameThread_GetRenderTargetResource());

		FString EventName;
		if (!CaptureComponent->ProfilingEventName.IsEmpty())
		{
			EventName = CaptureComponent->ProfilingEventName;
		}
		else if (CaptureComponent->GetOwner())
		{
			// The label might be non-unique, so include the actor name as well
			EventName = CaptureComponent->GetOwner()->GetActorNameOrLabel();

			FName ActorName = CaptureComponent->GetOwner()->GetFName();
			if (ActorName != EventName)
			{
				EventName.Appendf(TEXT(" (%s)"), *ActorName.ToString());
			}
		}

		const bool bGenerateMips = TextureTarget->bAutoGenerateMips;
		FGenerateMipsParams GenerateMipsParams{ TextureTarget->MipsSamplerFilter == TF_Nearest ? SF_Point : (TextureTarget->MipsSamplerFilter == TF_Trilinear ? SF_Trilinear : SF_Bilinear), AM_Clamp, AM_Clamp };

		FIntPoint CaptureSize(TextureTarget->GetSurfaceWidth(), TextureTarget->GetSurfaceHeight());
		const float FOVInDegrees = 90.f;
		const float FOVInRadians = FOVInDegrees * (float)PI / 360.0f;

		auto ComputeProjectionMatrix = [CaptureComponent, Transform, CaptureSize, FOVInRadians](ECubeFace TargetFace, FMatrix& OutViewRotationMatrix, FMatrix& OutProjectionMatrix)
		{
			if (CaptureComponent->bCaptureRotation)
			{
				OutViewRotationMatrix = Transform.ToInverseMatrixWithScale() * FLocal::CalcCubeFaceTransform(TargetFace);
			}
			else
			{
				OutViewRotationMatrix = FLocal::CalcCubeFaceTransform(TargetFace);
			}
			BuildProjectionMatrix(CaptureSize, FOVInRadians, GNearClippingPlane, OutProjectionMatrix);
		};

		const FVector Location = CaptureComponent->GetComponentToWorld().GetTranslation();

		bool bIsMultipleSceneCapture = CaptureComponent->SetFrameUpdated();
		bool bCaptureSceneColor = CaptureNeedsSceneColor(CaptureComponent->CaptureSource);

		if (GSceneCaptureCubeSinglePass == false)
		{
			// For GPU capture to work for multi-pass rendering, we need the capture scope to persist across all the scene render command lambdas,
			// so we need to allocate a pointer on the heap, and let the last render command clean it up.
			RenderCaptureInterface::FScopedCapture** ScopedCapturePtr;
			if (bCapturingGPU)
			{
				ScopedCapturePtr = new RenderCaptureInterface::FScopedCapture*;
				*ScopedCapturePtr = nullptr;
			}
			else
			{
				ScopedCapturePtr = nullptr;
			}

			for (int32 faceidx = 0; faceidx < (int32)ECubeFace::CubeFace_MAX; faceidx++)
			{
				const ECubeFace TargetFace = (ECubeFace)faceidx;

				FMatrix ViewRotationMatrix;
				FMatrix ProjectionMatrix;
				ComputeProjectionMatrix(TargetFace, ViewRotationMatrix, ProjectionMatrix);

				constexpr bool bCameraCut2D = false;
				constexpr bool bCopyMainViewTemporalSettings2D = false;
				FSceneRenderer* SceneRenderer = CreateSceneRendererForSceneCapture(this, CaptureComponent,
					TextureTarget->GameThread_GetRenderTargetResource(), CaptureSize, ViewRotationMatrix,
					Location, ProjectionMatrix, CaptureComponent->MaxViewDistanceOverride, FOVInDegrees,
					bCaptureSceneColor, bCameraCut2D, bCopyMainViewTemporalSettings2D, &CaptureComponent->PostProcessSettings, CaptureComponent->PostProcessBlendWeight, CaptureComponent->GetViewOwner(), faceidx);

				// When bIsMultipleSceneCapture is true, set bIsFirstSceneRenderer to false, which tells the scene renderer it can skip RHI resource flush, saving performance.
				// We can also skip RHI resource flush on faces after the first.
				SceneRenderer->bIsFirstSceneRenderer = (faceidx == 0) && !bIsMultipleSceneCapture;

				for (const FSceneViewExtensionRef& Extension : SceneRenderer->ViewFamily.ViewExtensions)
				{
					Extension->BeginRenderViewFamily(SceneRenderer->ViewFamily);
				}

				// Include the cube face index in the event name
				EventName.Appendf(TEXT(" [%d]"), faceidx);

				UE::RenderCommandPipe::FSyncScope SyncScope;

				ENQUEUE_RENDER_COMMAND(CaptureCommand)(
					[SceneRenderer, TextureRenderTarget, EventName, TargetFace, bGenerateMips, GenerateMipsParams, ScopedCapturePtr](FRHICommandListImmediate& RHICmdList)
					{
						if (ScopedCapturePtr && (int32)TargetFace == 0)
						{
							*ScopedCapturePtr = new RenderCaptureInterface::FScopedCapture(true, &RHICmdList, *FString::Format(TEXT("Scene Capture : {0}"), { EventName }));
						}

#if WITH_EDITOR
						// Scene renderer may be deleted in UpdateSceneCaptureContent_RenderThread, grab view state pointer first
						const FSceneViewState* ViewState = SceneRenderer->Views[0].ViewState;
#endif  // WITH_EDITOR

						VISUALIZE_TEXTURE_BEGIN_VIEW(SceneRenderer->FeatureLevel, SceneRenderer->Views[0].GetViewKey(), *EventName, true);

						// We need to generate mips on last cube face
						const bool bLastCubeFace = ((int32)TargetFace == (int32)ECubeFace::CubeFace_MAX - 1);

						FRHICopyTextureInfo CopyInfo;
						CopyInfo.DestSliceIndex = TargetFace;
						UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, TextureRenderTarget, TextureRenderTarget, EventName,
							TConstArrayView<FRHICopyTextureInfo>(&CopyInfo, 1), bGenerateMips && bLastCubeFace, GenerateMipsParams, true, false);

						VISUALIZE_TEXTURE_END_VIEW();

#if WITH_EDITOR
						if (ViewState)
						{
							const bool bLogSizes = GDumpSceneCaptureMemoryFrame == GFrameNumberRenderThread;
							if (bLogSizes)
							{
								UE_LOG(LogRenderer, Log, TEXT("LogSizes\tSceneCaptureCube[%d]\t%s\t%dx%d"), TargetFace, *EventName, TextureRenderTarget->GetSizeX(), TextureRenderTarget->GetSizeY());
								ViewState->GetGPUSizeBytes(bLogSizes);
							}
						}
#endif  // WITH_EDITOR

						if (ScopedCapturePtr && (int32)TargetFace == (int32)ECubeFace::CubeFace_MAX - 1)
						{
							// Delete the scope, and the persistent pointer we allocated on the heap to share across the render command lambdas
							delete *ScopedCapturePtr;
							delete ScopedCapturePtr;
						}
					}
				);

				// Trim the cube face index from the event name
				EventName.LeftChopInline(4, EAllowShrinking::No);
			}
		}
		else
		{
			TStaticArray<FSceneCaptureViewInfo, (int32)ECubeFace::CubeFace_MAX> SceneCaptureViewInfos;
			for (int32 faceidx = 0; faceidx < (int32)ECubeFace::CubeFace_MAX; faceidx++)
			{
				const ECubeFace TargetFace = (ECubeFace)faceidx;

				FMatrix ViewRotationMatrix;
				FMatrix ProjectionMatrix;
				ComputeProjectionMatrix(TargetFace, ViewRotationMatrix, ProjectionMatrix);

				FIntPoint ViewportOffset(GCubeFaceViewportOffsets[faceidx][0] * CaptureSize.X, GCubeFaceViewportOffsets[faceidx][1] * CaptureSize.Y);

				SceneCaptureViewInfos[faceidx].ViewRotationMatrix = ViewRotationMatrix;
				SceneCaptureViewInfos[faceidx].ViewOrigin = ViewLocation;
				SceneCaptureViewInfos[faceidx].ProjectionMatrix = ProjectionMatrix;
				SceneCaptureViewInfos[faceidx].StereoPass = EStereoscopicPass::eSSP_FULL;
				SceneCaptureViewInfos[faceidx].StereoViewIndex = INDEX_NONE;
				SceneCaptureViewInfos[faceidx].ViewRect = FIntRect(ViewportOffset.X, ViewportOffset.Y, ViewportOffset.X + CaptureSize.X, ViewportOffset.Y + CaptureSize.Y);
				SceneCaptureViewInfos[faceidx].FOV = 90.f;
			}

			// Render target that includes all six tiled faces of the cube map
			class FCubeFaceRenderTarget final : public FRenderTarget
			{
			public:
				FCubeFaceRenderTarget(FTextureRenderTargetCubeResource* InTextureRenderTarget)
				{
					// Cache a pointer to the output texture so we can get the pixel format later (InitRHI may not have been called on InTextureRenderTarget)
					TextureRenderTarget = InTextureRenderTarget;

					// Assume last cube face viewport offset is the furthest corner of the tiled cube face render target.
					// Add one to include the dimensions of the tile in addition to the offset.
					FIntPoint Size;
					Size.X = InTextureRenderTarget->GetSizeX() * (GCubeFaceViewportOffsets[(int32)ECubeFace::CubeFace_MAX - 1][0] + 1);
					Size.Y = InTextureRenderTarget->GetSizeY() * (GCubeFaceViewportOffsets[(int32)ECubeFace::CubeFace_MAX - 1][1] + 1);

					CubeFaceDesc = FPooledRenderTargetDesc::Create2DDesc(
						Size,
						PF_Unknown,							// Initialized in InitRHI below
						FClearValueBinding::Green,
						TexCreate_None,
						TexCreate_ShaderResource | TexCreate_RenderTargetable,
						false);
				}

				void InitRHI(FRHICommandListImmediate& RHICmdList)
				{
					// Set the format now that it's available
					CubeFaceDesc.Format = TextureRenderTarget->GetRenderTargetTexture()->GetFormat();

					GRenderTargetPool.FindFreeElement(RHICmdList, CubeFaceDesc, RenderTarget, TEXT("SceneCaptureTarget"));
					check(RenderTarget);

					RenderTargetTexture = RenderTarget->GetRHI();
				}

				// FRenderTarget interface
				const FTextureRHIRef& GetRenderTargetTexture() const override
				{
					return RenderTargetTexture;
				}

				FIntPoint GetSizeXY() const override { return CubeFaceDesc.Extent; }
				float GetDisplayGamma() const override { return 1.0f; }

			private:
				FTextureRenderTargetCubeResource* TextureRenderTarget;
				FPooledRenderTargetDesc CubeFaceDesc;
				TRefCountPtr<IPooledRenderTarget> RenderTarget;
				FTextureRHIRef RenderTargetTexture;
			};

			FCubeFaceRenderTarget* CubeFaceTarget = new FCubeFaceRenderTarget(TextureRenderTarget);

			// Copied from CreateSceneRendererForSceneCapture
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
				CubeFaceTarget,
				this,
				CaptureComponent->ShowFlags)
				.SetResolveScene(!bCaptureSceneColor)
				.SetRealtimeUpdate(CaptureComponent->bCaptureEveryFrame || CaptureComponent->bAlwaysPersistRenderingState));

			FSceneViewExtensionContext ViewExtensionContext(this);
			ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);

			TArray<FSceneView*> Views = SetupViewFamilyForSceneCapture(
				ViewFamily,
				CaptureComponent,
				SceneCaptureViewInfos,
				CaptureComponent->MaxViewDistanceOverride,
				bCaptureSceneColor,
				/* bIsPlanarReflection = */ false,
				&CaptureComponent->PostProcessSettings,
				CaptureComponent->PostProcessBlendWeight,
				CaptureComponent->GetViewOwner(),
				(int32)ECubeFace::CubeFace_MAX);			// Passing max cube face count indicates a view family with all faces

			// Scene capture source is used to determine whether to disable occlusion queries inside FSceneRenderer constructor
			ViewFamily.SceneCaptureSource = CaptureComponent->CaptureSource;

			// Screen percentage is still not supported in scene capture.
			ViewFamily.EngineShowFlags.ScreenPercentage = false;
			ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
				ViewFamily, /* GlobalResolutionFraction = */ 1.0f));

			// Call SetupViewFamily & SetupView on scene view extensions before renderer creation
			SetupSceneViewExtensionsForSceneCapture(ViewFamily, Views);

			FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(&ViewFamily, nullptr);

			// Need view state interface to be allocated for Lumen, as it requires persistent data.  This means
			// "bCaptureEveryFrame" or "bAlwaysPersistRenderingState" must be enabled.
			FSceneViewStateInterface* ViewStateInterface = CaptureComponent->GetViewState(0);

			if (ViewStateInterface &&
				(SceneRenderer->Views[0].FinalPostProcessSettings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::Lumen ||
				 SceneRenderer->Views[0].FinalPostProcessSettings.ReflectionMethod == EReflectionMethod::Lumen))
			{
				// It's OK to call these every frame -- they are no-ops if the correct data is already there
				ViewStateInterface->AddLumenSceneData(this, SceneRenderer->Views[0].FinalPostProcessSettings.LumenSurfaceCacheResolution);
			}
			else if (ViewStateInterface)
			{
				ViewStateInterface->RemoveLumenSceneData(this);
			}

			// When bIsMultipleSceneCapture is true, set bIsFirstSceneRenderer to false, which tells the scene renderer it can skip RHI resource flush, saving performance
			SceneRenderer->bIsFirstSceneRenderer = !bIsMultipleSceneCapture;

			for (const FSceneViewExtensionRef& Extension : SceneRenderer->ViewFamily.ViewExtensions)
			{
				Extension->BeginRenderViewFamily(SceneRenderer->ViewFamily);
			}

			UE::RenderCommandPipe::FSyncScope SyncScope;

			ENQUEUE_RENDER_COMMAND(CaptureAllCubeFaces)(
				[SceneRenderer, CubeFaceTarget, TextureRenderTarget, EventName, CaptureSize, bGenerateMips, GenerateMipsParams, bCapturingGPU](FRHICommandListImmediate& RHICmdList)
				{
					RenderCaptureInterface::FScopedCapture RenderCapture(bCapturingGPU, &RHICmdList, *FString::Format(TEXT("Scene Capture : {0}"), { EventName }));

					TStaticArray<FRHICopyTextureInfo, (int32)ECubeFace::CubeFace_MAX> CopyInfos;
					for (int32 faceidx = 0; faceidx < (int32)ECubeFace::CubeFace_MAX; faceidx++)
					{
						CopyInfos[faceidx].Size.X = CaptureSize.X;
						CopyInfos[faceidx].Size.Y = CaptureSize.Y;
						CopyInfos[faceidx].SourcePosition.X = GCubeFaceViewportOffsets[faceidx][0] * CaptureSize.X;
						CopyInfos[faceidx].SourcePosition.Y = GCubeFaceViewportOffsets[faceidx][1] * CaptureSize.Y;
						CopyInfos[faceidx].DestSliceIndex = faceidx;
					}

					CubeFaceTarget->InitRHI(RHICmdList);

#if WITH_EDITOR
					// Scene renderer may be deleted in UpdateSceneCaptureContent_RenderThread, grab view state pointer first
					TStaticArray<const FSceneViewState*, (int32)ECubeFace::CubeFace_MAX> SceneViewStates;
					for (int32 FaceIdx = 0; FaceIdx < (int32)ECubeFace::CubeFace_MAX; FaceIdx++)
					{
						SceneViewStates[FaceIdx] = SceneRenderer->Views[FaceIdx].ViewState;
					}
#endif  // WITH_EDITOR

					VISUALIZE_TEXTURE_BEGIN_VIEW(SceneRenderer->FeatureLevel, SceneRenderer->Views[0].GetViewKey(), *EventName, true);

					UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, CubeFaceTarget, TextureRenderTarget, EventName,
						CopyInfos, bGenerateMips, GenerateMipsParams, true, false);

					VISUALIZE_TEXTURE_END_VIEW();

#if WITH_EDITOR
					if (SceneViewStates[0])
					{
						const bool bLogSizes = GDumpSceneCaptureMemoryFrame == GFrameNumberRenderThread;
						if (bLogSizes)
						{
							UE_LOG(LogRenderer, Log, TEXT("LogSizes\tSceneCaptureCube\t%s\t%dx%d"), *EventName, CubeFaceTarget->GetSizeXY().X, CubeFaceTarget->GetSizeXY().Y);
							for (int32 FaceIdx = 0; FaceIdx < (int32)ECubeFace::CubeFace_MAX; FaceIdx++)
							{
								SceneViewStates[FaceIdx]->GetGPUSizeBytes(bLogSizes);
							}
						}
					}
#endif  // WITH_EDITOR

					delete CubeFaceTarget;
				}
			);
		}
	}

	EndGpuCaptureOrDump(bDumpingGPU);
}
