// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlatePostProcessor.h"
#include "SlateShaders.h"
#include "RendererUtils.h"
#include "Interfaces/SlateRHIRenderingPolicyInterface.h"

//////////////////////////////////////////////////////////////////////////

float GetSlateHDRUILevel()
{
	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Level"));
	return CVar ? CVar->GetFloat() : 1.0f;
}

float GetSlateHDRUILuminance()
{
	static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Luminance"));
	return CVar ? CVar->GetFloat() : 300.0f;
}

ETextureCreateFlags GetSlateTransientRenderTargetFlags()
{
	return ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource | ETextureCreateFlags::FastVRAM
		// Avoid fast clear metadata when this flag is set, since we'd otherwise have to clear transient render targets instead of discard.
#if PLATFORM_REQUIRES_TYPELESS_RESOURCE_DISCARD_WORKAROUND
		| ETextureCreateFlags::NoFastClear
#endif
		;
}

ETextureCreateFlags GetSlateTransientDepthStencilFlags()
{
	return ETextureCreateFlags::DepthStencilTargetable | ETextureCreateFlags::FastVRAM;
}

//////////////////////////////////////////////////////////////////////////

// Pixel shader to composite UI over HDR buffer prior to doing a blur
class FCompositeHDRForBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FCompositeHDRForBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FCompositeHDRForBlurPS, FGlobalShader);

	class FUseSRGBEncoding : SHADER_PERMUTATION_BOOL("SCRGB_ENCODING");
	using FPermutationDomain = TShaderPermutationDomain<FUseSRGBEncoding>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, UITexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, UIWriteMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, UISampler)
		SHADER_PARAMETER(float, UILevel)
		SHADER_PARAMETER(float, UILuminance)
		SHADER_PARAMETER(FVector2f, UITextureSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("COMPOSITE_UI_FOR_BLUR_PS"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCompositeHDRForBlurPS, "/Engine/Private/CompositeUIPixelShader.usf", "CompositeUIForBlur", SF_Pixel);

struct FSlateCompositeHDRForBlurPassInputs
{
	FIntRect InputRect;
	FRDGTexture* InputCompositeTexture;
	FRDGTexture* InputTexture;
	FIntPoint OutputExtent;
};

FScreenPassTexture AddSlateCompositeHDRForBlurPass(FRDGBuilder& GraphBuilder, const FSlateCompositeHDRForBlurPassInputs& Inputs)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	FRDGTexture* UIWriteMaskTexture = nullptr;

	if (RHISupportsRenderTargetWriteMask(GMaxRHIShaderPlatform))
	{
		FRenderTargetWriteMask::Decode(GraphBuilder, ShaderMap, MakeArrayView({ Inputs.InputCompositeTexture }), UIWriteMaskTexture, TexCreate_None, TEXT("UIRTWriteMask"));
	}

	FScreenPassRenderTarget Output(
		GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Inputs.OutputExtent, PF_FloatR11G11B10, FClearValueBinding::Black, GetSlateTransientRenderTargetFlags()),
			TEXT("CompositeHDRUI")),
		ERenderTargetLoadAction::ENoAction);

	const FScreenPassTextureViewport InputViewport = FScreenPassTextureViewport(Inputs.InputCompositeTexture, Inputs.InputRect);
	const FScreenPassTextureViewport OutputViewport = FScreenPassTextureViewport(Output);

	FCompositeHDRForBlurPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FCompositeHDRForBlurPS::FUseSRGBEncoding>(Inputs.InputTexture->Desc.Format == PF_FloatRGBA);

	FCompositeHDRForBlurPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompositeHDRForBlurPS::FParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->SceneTexture = Inputs.InputTexture;
	PassParameters->UITexture = Inputs.InputCompositeTexture;
	PassParameters->UIWriteMaskTexture = UIWriteMaskTexture;
	PassParameters->UISampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	PassParameters->UITextureSize = InputViewport.Extent;
	PassParameters->UILevel = GetSlateHDRUILevel();
	PassParameters->UILuminance = GetSlateHDRUILuminance();

	TShaderMapRef<FCompositeHDRForBlurPS> PixelShader(ShaderMap, PermutationVector);
	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("CompositeHDR"), FeatureLevel, OutputViewport, InputViewport, PixelShader, PassParameters);
	return Output;
}

//////////////////////////////////////////////////////////////////////////

class FSlatePostProcessDownsamplePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSlatePostProcessDownsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FSlatePostProcessDownsamplePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ElementTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ElementTextureSampler)
		SHADER_PARAMETER(FVector4f, ShaderParams)
		SHADER_PARAMETER(FVector4f, UVBounds)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSlatePostProcessDownsamplePS, "/Engine/Private/SlatePostProcessPixelShader.usf", "DownsampleMain", SF_Pixel);

struct FSlatePostProcessDownsamplePassInputs
{
	FScreenPassTexture InputTexture;
	FIntPoint OutputExtent;
};

FScreenPassTexture AddSlatePostProcessDownsamplePass(FRDGBuilder& GraphBuilder, const FSlatePostProcessDownsamplePassInputs& Inputs)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FSlatePostProcessDownsamplePS> PixelShader(ShaderMap);

	const FScreenPassRenderTarget Output(
		GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Inputs.OutputExtent, Inputs.InputTexture.Texture->Desc.Format, FClearValueBinding::None, GetSlateTransientRenderTargetFlags()), TEXT("DownsampleUI")),
		ERenderTargetLoadAction::ENoAction);

	const FScreenPassTextureViewport InputViewport(Inputs.InputTexture);
	const FScreenPassTextureViewportParameters InputParameters = GetScreenPassTextureViewportParameters(InputViewport);
	const FScreenPassTextureViewport OutputViewport(Output);

	FSlatePostProcessDownsamplePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSlatePostProcessDownsamplePS::FParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->ElementTexture = Inputs.InputTexture.Texture;
	PassParameters->ElementTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	PassParameters->ShaderParams = FVector4f(InputParameters.ExtentInverse.X, InputParameters.ExtentInverse.Y, 0.0f, 0.0f);
	PassParameters->UVBounds = FVector4f(InputParameters.UVViewportBilinearMin, InputParameters.UVViewportBilinearMax);

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DownsampleUI"), FeatureLevel, OutputViewport, InputViewport, PixelShader, PassParameters);
	return Output;
}

//////////////////////////////////////////////////////////////////////////

enum class ESlatePostProcessUpsampleOutputFormat
{
	SDR = 0,
	HDR_SCRGB,
	HDR_PQ10,
	MAX
};

class FSlatePostProcessUpsamplePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSlatePostProcessUpsamplePS);
	SHADER_USE_PARAMETER_STRUCT(FSlatePostProcessUpsamplePS, FGlobalShader);

	class FUpsampleOutputFormat : SHADER_PERMUTATION_ENUM_CLASS("UPSAMPLE_OUTPUT_FORMAT", ESlatePostProcessUpsampleOutputFormat);
	using FPermutationDomain = TShaderPermutationDomain<FUpsampleOutputFormat>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ElementTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ElementTextureSampler)
		SHADER_PARAMETER(FVector4f, ShaderParams)
		SHADER_PARAMETER(FVector4f, ShaderParams2)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSlatePostProcessUpsamplePS, "/Engine/Private/SlatePostProcessPixelShader.usf", "UpsampleMain", SF_Pixel);

struct FSlatePostProcessUpsampleInputs
{
	FScreenPassTexture InputTexture;
	FRDGTexture* OutputTextureToClear = nullptr;
	FRDGTexture* OutputTexture = nullptr;
	ERenderTargetLoadAction OutputLoadAction = ERenderTargetLoadAction::ELoad;

	const FSlateClippingOp* ClippingOp = nullptr;
	const FDepthStencilBinding* ClippingStencilBinding = nullptr;
	FIntRect ClippingElementsViewRect;

	FIntRect OutputRect;
	FVector4f CornerRadius = FVector4f::Zero();
};

void AddSlatePostProcessUpsamplePass(FRDGBuilder& GraphBuilder, const FSlatePostProcessUpsampleInputs& Inputs)
{
	FSlatePostProcessUpsamplePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSlatePostProcessUpsamplePS::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(Inputs.OutputTexture, Inputs.OutputLoadAction);

	if (Inputs.ClippingStencilBinding)
	{
		PassParameters->RenderTargets.DepthStencil = *Inputs.ClippingStencilBinding;
	}
	
	ESlatePostProcessUpsampleOutputFormat OutputFormat = ESlatePostProcessUpsampleOutputFormat::SDR;

	if (Inputs.OutputTextureToClear)
	{
		OutputFormat = Inputs.OutputTexture->Desc.Format == PF_FloatRGBA
			? ESlatePostProcessUpsampleOutputFormat::HDR_SCRGB
			: ESlatePostProcessUpsampleOutputFormat::HDR_PQ10;

		PassParameters->RenderTargets[1] = FRenderTargetBinding(Inputs.OutputTextureToClear, Inputs.OutputLoadAction);
	}

	FSlatePostProcessUpsamplePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FSlatePostProcessUpsamplePS::FUpsampleOutputFormat>(OutputFormat);

	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	TShaderMapRef<FSlatePostProcessUpsamplePS> PixelShader(ShaderMap, PermutationVector);

	const FScreenPassTextureViewport InputViewport(Inputs.InputTexture);
	const FScreenPassTextureViewport OutputViewport(Inputs.OutputTexture, Inputs.OutputRect);
	const FScreenPassTextureViewportParameters InputParameters = GetScreenPassTextureViewportParameters(InputViewport);

	PassParameters->ElementTexture = Inputs.InputTexture.Texture;
	PassParameters->ElementTextureSampler = Inputs.InputTexture.ViewRect == Inputs.OutputRect
		? TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI()
		: TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->ShaderParams = FVector4f(InputParameters.ViewportSize, InputParameters.UVViewportSize);
	PassParameters->ShaderParams2 = Inputs.CornerRadius;

	FRHIBlendState* BlendState = Inputs.CornerRadius == FVector4f::Zero() ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

	FScreenPassPipelineState PipelineState(VertexShader, PixelShader, BlendState);
	GetSlateClippingPipelineState(Inputs.ClippingOp, PipelineState.DepthStencilState, PipelineState.StencilRef);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Upsample"),
		PassParameters,
		ERDGPassFlags::Raster,
		[OutputViewport, InputViewport, ClippingElementsViewRect = Inputs.ClippingElementsViewRect, PipelineState, PixelShader, ClippingOp = Inputs.ClippingOp, PassParameters](FRDGAsyncTask, FRHICommandList& RHICmdList)
	{
		if (ClippingOp && ClippingOp->Method == EClippingMethod::Stencil)
		{
			// Stencil clipping quads have their own viewport.
			RHICmdList.SetViewport(ClippingElementsViewRect.Min.X, ClippingElementsViewRect.Min.Y, 0.0f, ClippingElementsViewRect.Max.X, ClippingElementsViewRect.Max.Y, 1.0f);

			// Stencil clipping will issue its own draw calls.
			SetSlateClipping(RHICmdList, ClippingOp, ClippingElementsViewRect);
		}

		RHICmdList.SetViewport(OutputViewport.Rect.Min.X, OutputViewport.Rect.Min.Y, 0.0f, OutputViewport.Rect.Max.X, OutputViewport.Rect.Max.Y, 1.0f);

		if (ClippingOp && ClippingOp->Method == EClippingMethod::Scissor)
		{
			SetSlateClipping(RHICmdList, ClippingOp, ClippingElementsViewRect);
		}

		SetScreenPassPipelineState(RHICmdList, PipelineState);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
		DrawScreenPass_PostSetup(RHICmdList, FScreenPassViewInfo(), OutputViewport, InputViewport, PipelineState, EScreenPassDrawFlags::None);
	});
}

//////////////////////////////////////////////////////////////////////////

class FSlatePostProcessBlurPS : public FGlobalShader
{
public:
	static const int32 MAX_BLUR_SAMPLES = 127 / 2;

	DECLARE_GLOBAL_SHADER(FSlatePostProcessBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FSlatePostProcessBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ElementTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ElementTextureSampler)
		SHADER_PARAMETER_ARRAY(FVector4f, WeightAndOffsets, [MAX_BLUR_SAMPLES])
		SHADER_PARAMETER(uint32, SampleCount)
		SHADER_PARAMETER(FVector4f, BufferSizeAndDirection)
		SHADER_PARAMETER(FVector4f, UVBounds)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSlatePostProcessBlurPS, "/Engine/Private/SlatePostProcessPixelShader.usf", "GaussianBlurMain", SF_Pixel);

void AddSlatePostProcessBlurPass(FRDGBuilder& GraphBuilder, const FSlatePostProcessBlurPassInputs& Inputs)
{
	RDG_EVENT_SCOPE(GraphBuilder, "GaussianBlur");

	const auto GetWeight = [](float Dist, float Strength)
	{
		float Strength2 = Strength*Strength;
		return (1.0f / FMath::Sqrt(2 * PI*Strength2))*FMath::Exp(-(Dist*Dist) / (2 * Strength2));
	};

	const auto GetWeightsAndOffset = [GetWeight](float Dist, float Sigma)
	{
		float Offset1 = Dist;
		float Weight1 = GetWeight(Offset1, Sigma);

		float Offset2 = Dist + 1;
		float Weight2 = GetWeight(Offset2, Sigma);

		float TotalWeight = Weight1 + Weight2;
		float Offset = 0;

		if (TotalWeight > 0)
		{
			Offset = (Weight1 * Offset1 + Weight2 * Offset2) / TotalWeight;
		}

		return FVector2f(TotalWeight, Offset);
	};

	const int32 SampleCount = FMath::DivideAndRoundUp(Inputs.KernelSize, 2u);

	// We need half of the sample count array because we're packing two samples into one float;
	TArray<FVector4f, FRDGArrayAllocator> WeightsAndOffsets;
	WeightsAndOffsets.AddUninitialized(SampleCount % 2 == 0 ? SampleCount / 2 : SampleCount / 2 + 1);
	WeightsAndOffsets[0] = FVector4f(FVector2f(GetWeight(0, Inputs.Strength), 0), GetWeightsAndOffset(1, Inputs.Strength) );

	for (uint32 X = 3, SampleIndex = 1; X < Inputs.KernelSize; X += 4, SampleIndex++)
	{
		WeightsAndOffsets[SampleIndex] = FVector4f(GetWeightsAndOffset((float)X, Inputs.Strength), GetWeightsAndOffset((float)(X + 2), Inputs.Strength));
	}

	FScreenPassTextureViewport OutputTextureViewport(Inputs.InputRect.Size());

	const EPixelFormat InputPixelFormat = Inputs.InputTexture->Desc.Format;

	// Defaults to the input UI texture unless a downsample / composite pass is needed.
	FScreenPassTexture BlurInputTexture(Inputs.InputTexture, Inputs.InputRect);

	// Need to composite the HDR scene texture with a separate SDR UI texture (which also does a downsample).
	if (Inputs.SDRCompositeUITexture)
	{
		FSlateCompositeHDRForBlurPassInputs CompositeInputs;
		CompositeInputs.InputRect = Inputs.InputRect;
		CompositeInputs.InputTexture = Inputs.InputTexture;
		CompositeInputs.InputCompositeTexture = Inputs.SDRCompositeUITexture;
		CompositeInputs.OutputExtent = OutputTextureViewport.Extent;

		BlurInputTexture = AddSlateCompositeHDRForBlurPass(GraphBuilder, CompositeInputs);
	}
	// Need to do an explicit downsample pass.
	else if (Inputs.DownsampleAmount > 0)
	{
		OutputTextureViewport = FScreenPassTextureViewport(GetDownscaledExtent(Inputs.InputRect.Size(), Inputs.DownsampleAmount));

		FSlatePostProcessDownsamplePassInputs DownsampleInputs;
		DownsampleInputs.InputTexture = BlurInputTexture;
		DownsampleInputs.OutputExtent = OutputTextureViewport.Extent;

		BlurInputTexture = AddSlatePostProcessDownsamplePass(GraphBuilder, DownsampleInputs);
	}

	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FSlatePostProcessBlurPS> PixelShader(ShaderMap);

	FScreenPassRenderTarget BlurOutputTexture(
		GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(OutputTextureViewport.Extent, InputPixelFormat, FClearValueBinding::None, GetSlateTransientRenderTargetFlags()), TEXT("SlateBlurHorizontalTexture")),
		ERenderTargetLoadAction::ENoAction);

	FSlatePostProcessBlurPS::FParameters* PassParameters = nullptr;

	{
		const FScreenPassTextureViewport BlurInputViewport(BlurInputTexture);
		const FScreenPassTextureViewportParameters BlurInputParameters = GetScreenPassTextureViewportParameters(BlurInputViewport);

		PassParameters = GraphBuilder.AllocParameters<FSlatePostProcessBlurPS::FParameters>();
		PassParameters->RenderTargets[0] = BlurOutputTexture.GetRenderTargetBinding();
		PassParameters->ElementTexture = BlurInputTexture.Texture;
		PassParameters->ElementTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SampleCount = SampleCount;
		PassParameters->BufferSizeAndDirection = FVector4f(BlurInputParameters.ExtentInverse, FVector2f(1.0f, 0.0f));
		PassParameters->UVBounds = FVector4f(BlurInputParameters.UVViewportBilinearMin, BlurInputParameters.UVViewportBilinearMax);

		check(PassParameters->WeightAndOffsets.Num() * sizeof(PassParameters->WeightAndOffsets[0]) >= WeightsAndOffsets.Num() * sizeof(WeightsAndOffsets[0]));
		FPlatformMemory::Memcpy(PassParameters->WeightAndOffsets.GetData(), WeightsAndOffsets.GetData(), WeightsAndOffsets.Num() * sizeof(WeightsAndOffsets[0]));

		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Horizontal"), FeatureLevel, FScreenPassTextureViewport(BlurOutputTexture), BlurInputViewport, PixelShader, PassParameters);
	}

	BlurInputTexture = BlurOutputTexture;
	BlurOutputTexture = FScreenPassRenderTarget(
		GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(OutputTextureViewport.Extent, InputPixelFormat, FClearValueBinding::None, GetSlateTransientRenderTargetFlags()), TEXT("SlateBlurVerticalTexture")),
		ERenderTargetLoadAction::ENoAction);

	{
		const FScreenPassTextureViewport BlurInputViewport(BlurInputTexture);
		const FScreenPassTextureViewportParameters BlurInputParameters = GetScreenPassTextureViewportParameters(BlurInputViewport);

		PassParameters = GraphBuilder.AllocParameters<FSlatePostProcessBlurPS::FParameters>();
		PassParameters->RenderTargets[0] = BlurOutputTexture.GetRenderTargetBinding();
		PassParameters->ElementTexture = BlurInputTexture.Texture;
		PassParameters->ElementTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SampleCount = SampleCount;
		PassParameters->BufferSizeAndDirection = FVector4f(BlurInputParameters.ExtentInverse, FVector2f(0.0f, 1.0f));
		PassParameters->UVBounds = FVector4f(BlurInputParameters.UVViewportBilinearMin, BlurInputParameters.UVViewportBilinearMax);

		check(PassParameters->WeightAndOffsets.Num() * sizeof(PassParameters->WeightAndOffsets[0]) >= WeightsAndOffsets.Num() * sizeof(WeightsAndOffsets[0]));
		FPlatformMemory::Memcpy(PassParameters->WeightAndOffsets.GetData(), WeightsAndOffsets.GetData(), WeightsAndOffsets.Num() * sizeof(WeightsAndOffsets[0]));

		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Vertical"), FeatureLevel, FScreenPassTextureViewport(BlurOutputTexture), BlurInputViewport, PixelShader, PassParameters);
	}

	FSlatePostProcessUpsampleInputs UpsampleInputs;
	UpsampleInputs.InputTexture = BlurOutputTexture;
	UpsampleInputs.OutputTextureToClear = Inputs.SDRCompositeUITexture;
	UpsampleInputs.OutputTexture = Inputs.OutputTexture;
	UpsampleInputs.OutputRect = Inputs.OutputRect;
	UpsampleInputs.ClippingOp = Inputs.ClippingOp;
	UpsampleInputs.ClippingStencilBinding = Inputs.ClippingStencilBinding;
	UpsampleInputs.ClippingElementsViewRect = Inputs.ClippingElementsViewRect;
	UpsampleInputs.CornerRadius = Inputs.CornerRadius;

	AddSlatePostProcessUpsamplePass(GraphBuilder, UpsampleInputs);
}

void AddSlatePostProcessBlurPass(FRDGBuilder& GraphBuilder, const FSlatePostProcessSimpleBlurPassInputs& SimpleInputs)
{
	const int32 MinKernelSize = 3;
	const int32 MaxKernelSize = 255;
	const int32 Downsample2Threshold = 9;
	const int32 Downsample4Threshold = 64;
	const float StrengthToKernelSize = 3.0f;
	const float MinStrength = 0.5f;

	float Strength = FMath::Max(MinStrength, SimpleInputs.Strength);
	int32 KernelSize = FMath::RoundToInt(Strength * StrengthToKernelSize);
	int32 DownsampleAmount = 0;

	if (KernelSize > Downsample2Threshold)
	{
		DownsampleAmount = KernelSize >= Downsample4Threshold ? 4 : 2;
		KernelSize /= DownsampleAmount;
	}

	// Kernel sizes must be odd
	if (KernelSize % 2 == 0)
	{
		++KernelSize;
	}

	if (DownsampleAmount > 0)
	{
		Strength /= DownsampleAmount;
	}

	KernelSize = FMath::Clamp(KernelSize, MinKernelSize, MaxKernelSize);

	FSlatePostProcessBlurPassInputs Inputs;
	Inputs.InputTexture     = SimpleInputs.InputTexture.Texture;
	Inputs.InputRect        = SimpleInputs.InputTexture.ViewRect;
	Inputs.OutputTexture    = SimpleInputs.OutputTexture.Texture;
	Inputs.OutputRect       = SimpleInputs.OutputTexture.ViewRect;
	Inputs.KernelSize       = KernelSize;
	Inputs.Strength         = Strength;
	Inputs.DownsampleAmount = DownsampleAmount;

	AddSlatePostProcessBlurPass(GraphBuilder, Inputs);
}

//////////////////////////////////////////////////////////////////////////

class FSlatePostProcessColorDeficiencyPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSlatePostProcessColorDeficiencyPS);
	SHADER_USE_PARAMETER_STRUCT(FSlatePostProcessColorDeficiencyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ElementTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ElementTextureSampler)
		SHADER_PARAMETER(float, ColorVisionDeficiencyType)
		SHADER_PARAMETER(float, ColorVisionDeficiencySeverity)
		SHADER_PARAMETER(float, bCorrectDeficiency)
		SHADER_PARAMETER(float, bSimulateCorrectionWithDeficiency)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSlatePostProcessColorDeficiencyPS, "/Engine/Private/SlatePostProcessColorDeficiencyPixelShader.usf", "ColorDeficiencyMain", SF_Pixel);

void AddSlatePostProcessColorDeficiencyPass(FRDGBuilder& GraphBuilder, const FSlatePostProcessColorDeficiencyPassInputs& Inputs)
{
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FSlatePostProcessColorDeficiencyPS> PixelShader(ShaderMap);
	const FRDGTextureDesc& InputDesc = Inputs.InputTexture.Texture->Desc;

	const FScreenPassRenderTarget Output(
		GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(InputDesc.Extent, InputDesc.Format, FClearValueBinding::None, GetSlateTransientRenderTargetFlags()), TEXT("ColorDeficiency")),
		ERenderTargetLoadAction::ENoAction);

	FSlatePostProcessColorDeficiencyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSlatePostProcessColorDeficiencyPS::FParameters>();
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->ElementTexture = Inputs.InputTexture.Texture;
	PassParameters->ElementTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->ColorVisionDeficiencyType = (float)GSlateColorDeficiencyType;
	PassParameters->ColorVisionDeficiencySeverity = (float)GSlateColorDeficiencySeverity;
	PassParameters->bCorrectDeficiency = GSlateColorDeficiencyCorrection ? 1.0f : 0.0f;
	PassParameters->bSimulateCorrectionWithDeficiency = GSlateShowColorDeficiencyCorrectionWithDeficiency ? 1.0f : 0.0f;

	const FScreenPassTextureViewport Viewport(Output);
	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("ColorDeficiency"), FeatureLevel, Viewport, Viewport, PixelShader, PassParameters);

	FSlatePostProcessUpsampleInputs UpsampleInputs;
	UpsampleInputs.InputTexture = Output;
	UpsampleInputs.OutputTexture = Inputs.OutputTexture.Texture;
	UpsampleInputs.OutputRect = Inputs.OutputTexture.ViewRect;

	AddSlatePostProcessUpsamplePass(GraphBuilder, UpsampleInputs);
}