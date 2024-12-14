// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionSceneViewExtension.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "GlobalShader.h"
#include "PostProcess/LensDistortion.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"

TAutoConsoleVariable<float> CVarLensDistortionDisplacementOverscan(
	TEXT("r.LensDistortion.DisplacementMapOverscan"),
	1.25f,
	TEXT("A factor to scale the distortion displacement map to ensure that the undistortion map is properly invertible.\n")
	TEXT("Value is clamped between 1.0 (no overscan) and 2.0.\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarLensDistortionInvertGridDensity(
	TEXT("r.LensDistortion.InvertGridDensity"),
	64,
	TEXT("The number of squares drawn by the shader that inverts the distortion displacement map\n")
	TEXT("Value is clamped between 64 and 255.\n"),
	ECVF_RenderThreadSafe);

FLensDistortionSceneViewExtension::FLensDistortionSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FLensDistortionSceneViewExtension::UpdateDistortionState_AnyThread(ACameraActor* CameraActor, FDisplacementMapBlendingParams DistortionState)
{
	FScopeLock ScopeLock(&DistortionStateMapCriticalSection);
	DistortionStateMap.Add(CameraActor, DistortionState);
}

void FLensDistortionSceneViewExtension::ClearDistortionState_AnyThread(ACameraActor* CameraActor)
{
	FScopeLock ScopeLock(&DistortionStateMapCriticalSection);
	DistortionStateMap.Remove(CameraActor);
}

class FDrawDistortionDisplacementMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDrawDistortionDisplacementMapCS);
	SHADER_USE_PARAMETER_STRUCT(FDrawDistortionDisplacementMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, ThreadIdToUV)
		SHADER_PARAMETER(FVector2f, FocalLength)
		SHADER_PARAMETER(FVector2f, ImageCenter)
		SHADER_PARAMETER(float, K1)
		SHADER_PARAMETER(float, K2)
		SHADER_PARAMETER(float, K3)
		SHADER_PARAMETER(float, P1)
		SHADER_PARAMETER(float, P2)
		SHADER_PARAMETER(float, InverseOverscan)
		SHADER_PARAMETER(float, CameraOverscan)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutDistortionMap)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FDrawDistortionDisplacementMapCS, "/Plugin/CameraCalibrationCore/Private/DrawDisplacementMaps.usf", "MainCS", SF_Compute);


class FBlendDistortionDisplacementMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBlendDistortionDisplacementMapCS);
	SHADER_USE_PARAMETER_STRUCT(FBlendDistortionDisplacementMapCS, FGlobalShader);

	class FBlendType : SHADER_PERMUTATION_INT("BLEND_TYPE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FBlendType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, ThreadIdToUV)
		SHADER_PARAMETER(FVector2f, FxFyScale)
		SHADER_PARAMETER_ARRAY(FVector4f, PatchCorners, [4])
		SHADER_PARAMETER(float, EvalFocus)
		SHADER_PARAMETER(float, EvalZoom)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputDistortionMap1)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputDistortionMap2)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputDistortionMap3)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputDistortionMap4)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceTextureSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OverscanDistortionMap)
	END_SHADER_PARAMETER_STRUCT()

	// Called by the engine to determine which permutations to compile for this shader
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FBlendDistortionDisplacementMapCS, "/Plugin/CameraCalibrationCore/Private/BlendDisplacementMaps.usf", "MainCS", SF_Compute);

class FCropDistortionDisplacementMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCropDistortionDisplacementMapCS);
	SHADER_USE_PARAMETER_STRUCT(FCropDistortionDisplacementMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InDistortionMapWithOverscan)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutDistortionMap)
		SHADER_PARAMETER(FIntPoint, OverscanOffset)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCropDistortionDisplacementMapCS, "/Plugin/CameraCalibrationCore/Private/CropDisplacementMap.usf", "MainCS", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FInvertDisplacementParameters, )
	SHADER_PARAMETER(FIntPoint, GridDimensions)
	SHADER_PARAMETER(FVector2f, PixelToUV)
	SHADER_PARAMETER(FVector2f, PixelToOverscanUV)
	SHADER_PARAMETER(float, OverscanFactor)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DistortionMap)
	SHADER_PARAMETER_SAMPLER(SamplerState, DistortionMapSampler)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FInvertDisplacementVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FInvertDisplacementVS);
	SHADER_USE_PARAMETER_STRUCT(FInvertDisplacementVS, FGlobalShader);
	using FParameters = FInvertDisplacementParameters;
};

class FInvertDisplacementPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FInvertDisplacementPS);
	SHADER_USE_PARAMETER_STRUCT(FInvertDisplacementPS, FGlobalShader);
	using FParameters = FInvertDisplacementParameters;
};

IMPLEMENT_GLOBAL_SHADER(FInvertDisplacementVS, "/Plugin/CameraCalibrationCore/Private/InvertDisplacementMap.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FInvertDisplacementPS, "/Plugin/CameraCalibrationCore/Private/InvertDisplacementMap.usf", "MainPS", SF_Pixel);

void FLensDistortionSceneViewExtension::DrawDisplacementMap_RenderThread(FRDGBuilder& GraphBuilder, const FLensDistortionState& CurrentState, float InverseOverscan, float CameraOverscan, FRDGTextureRef& OutDistortionMapWithOverscan)
{
	if (CurrentState.DistortionInfo.Parameters.IsEmpty())
	{
		OutDistortionMapWithOverscan = GSystemTextures.GetBlackDummy(GraphBuilder);
		return;
	}

	FDrawDistortionDisplacementMapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDrawDistortionDisplacementMapCS::FParameters>();

	PassParameters->OutDistortionMap = GraphBuilder.CreateUAV(OutDistortionMapWithOverscan);

	FIntPoint DistortionMapResolution = OutDistortionMapWithOverscan->Desc.Extent;
	PassParameters->ThreadIdToUV = FVector2f(1.0f) / FVector2f(DistortionMapResolution);

	PassParameters->ImageCenter = FVector2f(CurrentState.ImageCenter.PrincipalPoint);
	PassParameters->FocalLength = FVector2f(CurrentState.FocalLengthInfo.FxFy);

	PassParameters->K1 = CurrentState.DistortionInfo.Parameters[0];
	PassParameters->K2 = CurrentState.DistortionInfo.Parameters[1];
	PassParameters->K3 = CurrentState.DistortionInfo.Parameters[2];
	PassParameters->P1 = CurrentState.DistortionInfo.Parameters[3];
	PassParameters->P2 = CurrentState.DistortionInfo.Parameters[4];

	PassParameters->InverseOverscan = InverseOverscan;
	PassParameters->CameraOverscan = CameraOverscan;

	TShaderMapRef<FDrawDistortionDisplacementMapCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("DrawDistortionDisplacementMap"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp(DistortionMapResolution.X, 8), FMath::DivideAndRoundUp(DistortionMapResolution.Y, 8), 1));
}

void FLensDistortionSceneViewExtension::CropDisplacementMap_RenderThread(FRDGBuilder& GraphBuilder, const FRDGTextureRef& InDistortionMapWithOverscan, FRDGTextureRef& OutDistortionMap)
{
	FCropDistortionDisplacementMapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCropDistortionDisplacementMapCS::FParameters>();

	PassParameters->InDistortionMapWithOverscan = GraphBuilder.CreateSRV(InDistortionMapWithOverscan);
	PassParameters->OutDistortionMap = GraphBuilder.CreateUAV(OutDistortionMap);

	FIntPoint LUTResolution = OutDistortionMap->Desc.Extent;
	PassParameters->OverscanOffset = (InDistortionMapWithOverscan->Desc.Extent - OutDistortionMap->Desc.Extent) / 2;

	TShaderMapRef<FCropDistortionDisplacementMapCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CropDistortionDisplacementMap"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp(LUTResolution.X, 8), FMath::DivideAndRoundUp(LUTResolution.Y, 8), 1));
}

void FLensDistortionSceneViewExtension::BlendDisplacementMaps_RenderThread(FRDGBuilder& GraphBuilder, const FDisplacementMapBlendingParams& BlendState, float InverseOverscan, float CameraOverscan, FRDGTextureRef& OutDistortionMapWithOverscan)
{
	FBlendDistortionDisplacementMapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBlendDistortionDisplacementMapCS::FParameters>();

	// Draw the first distortion map, which should always be valid
	{
		FRDGTextureRef Distortion1 = GraphBuilder.CreateTexture(OutDistortionMapWithOverscan->Desc, TEXT("DistortingDisplacement1"));
		DrawDisplacementMap_RenderThread(GraphBuilder, BlendState.States[0], InverseOverscan, CameraOverscan, Distortion1);
		PassParameters->InputDistortionMap1 = GraphBuilder.CreateSRV(Distortion1);
	}

	// Draw the second distortion map if any blend is needed
	if (BlendState.BlendType != EDisplacementMapBlendType::OneFocusOneZoom)
	{
		FRDGTextureRef Distortion2 = GraphBuilder.CreateTexture(OutDistortionMapWithOverscan->Desc, TEXT("DistortingDisplacement2"));
		DrawDisplacementMap_RenderThread(GraphBuilder, BlendState.States[1], InverseOverscan, CameraOverscan, Distortion2);
		PassParameters->InputDistortionMap2 = GraphBuilder.CreateSRV(Distortion2);
	}

	// Draw the 3rd and 4th distortion maps if a 4-way blend is needed
	if (BlendState.BlendType == EDisplacementMapBlendType::TwoFocusTwoZoom)
	{
		FRDGTextureRef Distortion3 = GraphBuilder.CreateTexture(OutDistortionMapWithOverscan->Desc, TEXT("DistortingDisplacement3"));
		FRDGTextureRef Distortion4 = GraphBuilder.CreateTexture(OutDistortionMapWithOverscan->Desc, TEXT("DistortingDisplacement4"));

		DrawDisplacementMap_RenderThread(GraphBuilder, BlendState.States[2], InverseOverscan, CameraOverscan, Distortion3);
		DrawDisplacementMap_RenderThread(GraphBuilder, BlendState.States[3], InverseOverscan, CameraOverscan, Distortion4);

		PassParameters->InputDistortionMap3 = GraphBuilder.CreateSRV(Distortion3);
		PassParameters->InputDistortionMap4 = GraphBuilder.CreateSRV(Distortion4);
	}

	PassParameters->OverscanDistortionMap = GraphBuilder.CreateUAV(OutDistortionMapWithOverscan);
	PassParameters->SourceTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const FIntPoint DistortionMapResolution = OutDistortionMapWithOverscan->Desc.Extent;
	PassParameters->ThreadIdToUV = FVector2f(1.0f / DistortionMapResolution.X, 1.0f / DistortionMapResolution.Y);

	PassParameters->FxFyScale = FVector2f(BlendState.FxFyScale);

	// Set permutation and blending params based on blend type
	PassParameters->EvalFocus = BlendState.EvalFocus;
	PassParameters->EvalZoom = BlendState.EvalZoom;

	FBlendDistortionDisplacementMapCS::FPermutationDomain PermutationVector;
	if (BlendState.BlendType == EDisplacementMapBlendType::OneFocusOneZoom)
	{
		PermutationVector.Set<FBlendDistortionDisplacementMapCS::FBlendType>(0);
	}
	else if (BlendState.BlendType == EDisplacementMapBlendType::TwoFocusOneZoom)
	{
		PermutationVector.Set<FBlendDistortionDisplacementMapCS::FBlendType>(1);
		PassParameters->PatchCorners[0] = BlendState.PatchCorners[0].ToVector();
		PassParameters->PatchCorners[1] = BlendState.PatchCorners[1].ToVector();
		PassParameters->PatchCorners[2] = FVector4f::Zero();
		PassParameters->PatchCorners[3] = FVector4f::Zero();
	}
	else if (BlendState.BlendType == EDisplacementMapBlendType::OneFocusTwoZoom)
	{
		PermutationVector.Set<FBlendDistortionDisplacementMapCS::FBlendType>(2);
		PassParameters->PatchCorners[0] = BlendState.PatchCorners[0].ToVector();
		PassParameters->PatchCorners[1] = BlendState.PatchCorners[1].ToVector();
		PassParameters->PatchCorners[2] = FVector4f::Zero();
		PassParameters->PatchCorners[3] = FVector4f::Zero();
	}
	else if (BlendState.BlendType == EDisplacementMapBlendType::TwoFocusTwoZoom)
	{
		PermutationVector.Set<FBlendDistortionDisplacementMapCS::FBlendType>(3);
		PassParameters->PatchCorners[0] = BlendState.PatchCorners[0].ToVector();
		PassParameters->PatchCorners[1] = BlendState.PatchCorners[1].ToVector();
		PassParameters->PatchCorners[2] = BlendState.PatchCorners[2].ToVector();
		PassParameters->PatchCorners[3] = BlendState.PatchCorners[3].ToVector();
	}

	TShaderMapRef<FBlendDistortionDisplacementMapCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("BlendDistortionDisplacementMap"),
		ComputeShader,
		PassParameters,
		FIntVector(FMath::DivideAndRoundUp(DistortionMapResolution.X, 8), FMath::DivideAndRoundUp(DistortionMapResolution.Y, 8), 1));
}

void FLensDistortionSceneViewExtension::InvertDistortionMap_RenderThread(FRDGBuilder& GraphBuilder, const FRDGTextureRef& InDistortionMap, FRDGTextureRef& OutUndistortionMap)
{
	FInvertDisplacementParameters* PassParameters = GraphBuilder.AllocParameters<FInvertDisplacementParameters>();

	FScreenPassRenderTarget Output;
	Output.Texture = OutUndistortionMap;
	Output.ViewRect = FIntRect(FIntPoint(0, 0), OutUndistortionMap->Desc.Extent);
	Output.LoadAction = ERenderTargetLoadAction::EClear;
	Output.UpdateVisualizeTextureExtent();

	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	PassParameters->DistortionMap = GraphBuilder.CreateSRV(InDistortionMap);
	PassParameters->DistortionMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->OverscanFactor = float(InDistortionMap->Desc.Extent.X) / float(OutUndistortionMap->Desc.Extent.X);
	PassParameters->PixelToUV = FVector2f(1.0f) / FVector2f(OutUndistortionMap->Desc.Extent);
	PassParameters->PixelToOverscanUV = FVector2f(1.0f) / FVector2f(InDistortionMap->Desc.Extent);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("InvertDistortionDisplacementMap"),
		PassParameters,
		ERDGPassFlags::Raster,
		[PassParameters, Output](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			const int32 NumSquares = FMath::Clamp(CVarLensDistortionInvertGridDensity.GetValueOnRenderThread(), 64, 255);
			const FIntPoint GridDimensions = FIntPoint(NumSquares);
			PassParameters->GridDimensions = GridDimensions;

			RHICmdList.SetViewport(Output.ViewRect.Min.X, Output.ViewRect.Min.Y, 0.0f, Output.ViewRect.Max.X, Output.ViewRect.Max.Y, 1.0f);

			TShaderMapRef<FInvertDisplacementVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			TShaderMapRef<FInvertDisplacementPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			SetScreenPassPipelineState(RHICmdList, FScreenPassPipelineState(VertexShader, PixelShader));
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			FRHIBatchedShaderParameters& BatchedParameters = RHICmdList.GetScratchShaderParameters();
			RHICmdList.SetBatchedShaderParameters(VertexShader.GetVertexShader(), BatchedParameters);

			// No vertex buffer is needed because we compute it in the VS
			RHICmdList.SetStreamSource(0, NULL, 0);

			// The following code for setting up this index buffer based on FTesselatedScreenRectangleIndexBuffer::InitRHI()
			TResourceArray<uint16> IndexBuffer;

			const uint32 Width = GridDimensions.X;
			const uint32 Height = GridDimensions.Y;
			const uint32 NumTriangles = Width * Height * 2;
			const uint32 NumIndices = NumTriangles * 3;

			IndexBuffer.AddUninitialized(NumIndices);

			uint16* IndexBufferData = (uint16*)IndexBuffer.GetData();

			for (uint32 IndexY = 0; IndexY < Height; ++IndexY)
			{
				for (uint32 IndexX = 0; IndexX < Width; ++IndexX)
				{
					// left top to bottom right in reading order
					uint16 Index00 = IndexX + IndexY * (Width + 1);
					uint16 Index10 = Index00 + 1;
					uint16 Index01 = Index00 + (Width + 1);
					uint16 Index11 = Index01 + 1;

					// triangle A
					*IndexBufferData++ = Index00;
					*IndexBufferData++ = Index01;
					*IndexBufferData++ = Index10;

					// triangle B
					*IndexBufferData++ = Index11;
					*IndexBufferData++ = Index10;
					*IndexBufferData++ = Index01;
				}
			}

			// Create index buffer. Fill buffer with initial data upon creation
			FRHIResourceCreateInfo CreateInfo(TEXT("InvertDistortionMapIndexBuffer"), &IndexBuffer);
			FBufferRHIRef IndexBufferRHI = RHICmdList.CreateIndexBuffer(sizeof(uint16), IndexBuffer.GetResourceDataSize(), BUF_Static, CreateInfo);

			RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, 0, 0, NumIndices, 0, NumTriangles, 1);
		});
}

void FLensDistortionSceneViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	if (const ACameraActor* CameraActor = Cast<const ACameraActor>(InView.ViewActor))
	{
		FDisplacementMapBlendingParams BlendState;
		{
			FScopeLock ScopeLock(&DistortionStateMapCriticalSection);
			if (FDisplacementMapBlendingParams* BlendStatePtr = DistortionStateMap.Find(CameraActor))
			{
				BlendState = *BlendStatePtr;
			}
			else
			{
				return;
			}
		}

		// Create the distortion map and undistortion map textures for the FLensDistortionLUT for this frame
		const FIntPoint DisplacementMapResolution = FIntPoint(256, 256);

		FRDGTextureDesc DistortionMapDesc = FRDGTextureDesc::Create2D(
			DisplacementMapResolution,
			PF_G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureDesc UndistortionMapDesc = FRDGTextureDesc::Create2D(
			DisplacementMapResolution,
			PF_G32R32F,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_RenderTargetable);

		FLensDistortionLUT ViewDistortionLUT;

		ViewDistortionLUT.DistortingDisplacementTexture = GraphBuilder.CreateTexture(DistortionMapDesc, TEXT("DistortionDisplacementMap"));
		ViewDistortionLUT.UndistortingDisplacementTexture = GraphBuilder.CreateTexture(UndistortionMapDesc, TEXT("UndistortionDisplacementMap"));

		// In order to guarantee that we can generate a complete undistortion map, the distortion map we invert needs to have some overscan 
		float InverseOverscan = FMath::Clamp(CVarLensDistortionDisplacementOverscan.GetValueOnRenderThread(), 1.0f, 2.0f);

		// Adjust the overscan resolution to be square, with each side being a multiple of 8
		const FIntPoint OverscanResolution = FIntPoint(FMath::CeilToInt(InverseOverscan * 32) * 8);
		InverseOverscan = float(OverscanResolution.X) / float(DisplacementMapResolution.X);

		// Create the texture for the overscanned distortion map
		FRDGTextureDesc OverscanDesc = FRDGTextureDesc::Create2D(
			OverscanResolution,
			PF_G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);

		FRDGTextureRef DistortionMapWithOverscan = GraphBuilder.CreateTexture(OverscanDesc, TEXT("DistortionMapWithOverscan"));

		float CameraOverscan = 1.0f;
		if (UCameraComponent* CameraComponent = CameraActor->GetCameraComponent())
		{
			CameraOverscan = CameraComponent->Overscan + 1.0f;
		}

		BlendDisplacementMaps_RenderThread(GraphBuilder, BlendState, InverseOverscan, CameraOverscan, DistortionMapWithOverscan);
		InvertDistortionMap_RenderThread(GraphBuilder, DistortionMapWithOverscan, ViewDistortionLUT.UndistortingDisplacementTexture);
		CropDisplacementMap_RenderThread(GraphBuilder, DistortionMapWithOverscan, ViewDistortionLUT.DistortingDisplacementTexture);

		LensDistortion::SetLUTUnsafe(InView, ViewDistortionLUT);
	}
}
