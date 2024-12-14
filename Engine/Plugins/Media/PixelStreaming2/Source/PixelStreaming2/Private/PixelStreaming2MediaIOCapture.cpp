// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2MediaIOCapture.h"

#include "Logging.h"
#include "MediaShaders.h"
#include "PixelCaptureInputFrameRHI.h"
#include "PixelStreaming2Module.h"
#include "RenderGraphUtils.h"
#include "RHI.h"
#include "ScreenPass.h"
#include "ScreenRendering.h"
#include "Slate/SceneViewport.h"

void UPixelStreaming2MediaIOCapture::OnRHIResourceCaptured_RenderingThread(
	FRHICommandListImmediate& /* RHICmdList */,
	const FCaptureBaseData&								   InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	FTextureRHIRef										   InTexture)
{
	HandleCapturedFrame(InTexture);
}

void UPixelStreaming2MediaIOCapture::OnCustomCapture_RenderingThread(
	FRDGBuilder&										   GraphBuilder,
	const FCaptureBaseData&								   InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	FRDGTextureRef										   InSourceTexture,
	FRDGTextureRef										   OutputTexture,
	const FRHICopyTextureInfo&							   CopyInfo,
	FVector2D											   CropU,
	FVector2D											   CropV)
{
	bool bRequiresFormatConversion = InSourceTexture->Desc.Format != OutputTexture->Desc.Format;
	if (InSourceTexture->Desc.Format == OutputTexture->Desc.Format
		&& InSourceTexture->Desc.Extent.X == OutputTexture->Desc.Extent.X
		&& InSourceTexture->Desc.Extent.Y == OutputTexture->Desc.Extent.Y)
	{
		// The formats are the same and size are the same. simple copy
		AddDrawTexturePass(
			GraphBuilder,
			GetGlobalShaderMap(GMaxRHIFeatureLevel),
			InSourceTexture,
			OutputTexture,
			FRDGDrawTextureInfo());

		return;
	}
	else
	{
#if PLATFORM_MAC
		// Create a staging texture that is the same size and format as the final.
		FRDGTextureRef			   StagingTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(OutputTexture->Desc.Extent.X, OutputTexture->Desc.Extent.Y), OutputTexture->Desc.Format, OutputTexture->Desc.ClearValue, ETextureCreateFlags::RenderTargetable), TEXT("PixelStreamingMediaIOCapture Staging"));
		FScreenPassTextureViewport StagingViewport(StagingTexture);
#endif

		FScreenPassTextureViewport InputViewport(InSourceTexture);
		FScreenPassTextureViewport OutputViewport(OutputTexture);

		FGlobalShaderMap*			 GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FScreenPassVS> VertexShader(GlobalShaderMap);

		// In cases where texture is converted from a format that doesn't have A channel, we want to force set it to 1.
		int32										  MediaConversionOperation = 0; // None
		FModifyAlphaSwizzleRgbaPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FModifyAlphaSwizzleRgbaPS::FConversionOp>(MediaConversionOperation);

		// Rectangle area to use from source
		const FIntRect ViewRect(FIntPoint(0, 0), InSourceTexture->Desc.Extent);

		TShaderMapRef<FModifyAlphaSwizzleRgbaPS> PixelShader(GlobalShaderMap, PermutationVector);
		FModifyAlphaSwizzleRgbaPS::FParameters*	 PixelShaderParameters = PixelShader->AllocateAndSetParameters(
			 GraphBuilder,
			 InSourceTexture,
#if PLATFORM_MAC
			StagingTexture
#else
			OutputTexture
#endif
		);

		FRHIBlendState*		   BlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
		FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();

		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("PixelStreaming2MediaIOCapture Swizzle"),
			FScreenPassViewInfo(),
#if PLATFORM_MAC
			StagingViewport,
#else
			OutputViewport,
#endif

			InputViewport,
			VertexShader,
			PixelShader,
			PixelShaderParameters);

#if PLATFORM_MAC
		// Now we can be certain the formats are the same and size are the same. simple copy
		AddDrawTexturePass(
			GraphBuilder,
			GetGlobalShaderMap(GMaxRHIFeatureLevel),
			StagingTexture,
			OutputTexture,
			FRDGDrawTextureInfo());
#endif
	}
}

bool UPixelStreaming2MediaIOCapture::InitializeCapture()
{
	UE_LOG(LogPixelStreaming2, Log, TEXT("Initializing Media IO capture for Pixel Streaming."));
	bViewportResized = false;
	bDoGPUCopy = true;

	SetState(EMediaCaptureState::Capturing);

	return true;
}

void UPixelStreaming2MediaIOCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	// Todo: Any cleanup on capture stop should happen here.
}

ETextureCreateFlags UPixelStreaming2MediaIOCapture::GetOutputTextureFlags() const
{
#if PLATFORM_MAC
	return TexCreate_CPUReadback;
#else
	ETextureCreateFlags Flags = TexCreate_RenderTargetable | TexCreate_UAV;

	if (RHIGetInterfaceType() == ERHIInterfaceType::Vulkan)
	{
		Flags |= TexCreate_External;
	}
	else if (RHIGetInterfaceType() == ERHIInterfaceType::D3D11 || RHIGetInterfaceType() == ERHIInterfaceType::D3D12)
	{
		Flags |= TexCreate_Shared;
	}

	return Flags;
#endif
}

bool UPixelStreaming2MediaIOCapture::PostInitializeCaptureViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	SceneViewport = TWeakPtr<FSceneViewport>(InSceneViewport);
	OnCaptureViewportInitialized.Broadcast();

	// Listen for viewport resize events as resizes invalidate media capture, so we want to know when to reset capture
	InSceneViewport->ViewportResizedEvent.AddUObject(this, &UPixelStreaming2MediaIOCapture::ViewportResized);

	return true;
}

void UPixelStreaming2MediaIOCapture::ViewportResized(FViewport* Viewport, uint32 ResizeCode)
{
	// If we have not captured a frame yet, we don't care about stopping the capture due to capture size mismatch
	if (!CaptureResolution)
	{
		return;
	}

	// If resolution of viewport is actually the same as the capture resolution no need to stop/restart capturing.
	if (Viewport->GetSizeXY() == *CaptureResolution)
	{
		return;
	}

	bViewportResized = true;
	if (GetState() == EMediaCaptureState::Capturing)
	{
		UE_LOG(LogPixelStreaming2, Warning, TEXT("Stopping PixelStreaming MediaIO capture because viewport was resized."));
		StopCapture(false);
	}
}

void UPixelStreaming2MediaIOCapture::HandleCapturedFrame(FTextureRHIRef InTexture)
{
	TSharedPtr<IPixelStreaming2VideoProducer> VideoProducerPtr = VideoProducer.Pin();
	if (VideoProducerPtr)
	{
		UpdateCaptureResolution(InTexture->GetDesc().Extent.X, InTexture->GetDesc().Extent.Y);
		VideoProducerPtr->PushFrame(FPixelCaptureInputFrameRHI(InTexture));
	}
}

void UPixelStreaming2MediaIOCapture::UpdateCaptureResolution(int32 Width, int32 Height)
{
	if (!CaptureResolution)
	{
		CaptureResolution = MakeUnique<FIntPoint>();
	}
	CaptureResolution->X = Width;
	CaptureResolution->Y = Height;
}
