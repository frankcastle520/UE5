// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoProducerPIEViewport.h"

#include "Engine/GameViewportClient.h"
#include "PixelCaptureInputFrameRHI.h"
#include "RenderingThread.h"
#include "UnrealClient.h"
#include "UtilsCommon.h"

namespace UE::PixelStreaming2
{

	TSharedPtr<FVideoProducerPIEViewport> FVideoProducerPIEViewport::Create()
	{
		TSharedPtr<FVideoProducerPIEViewport> NewInput = TSharedPtr<FVideoProducerPIEViewport>(new FVideoProducerPIEViewport());
		TWeakPtr<FVideoProducerPIEViewport>	  WeakInput = NewInput;

		UE::PixelStreaming2::DoOnGameThread([WeakInput]() {
			if (TSharedPtr<FVideoProducerPIEViewport> Input = WeakInput.Pin())
			{
				Input->DelegateHandle = UGameViewportClient::OnViewportRendered().AddSP(Input.ToSharedRef(), &FVideoProducerPIEViewport::OnViewportRendered);
			}
		});

		return NewInput;
	}

	FVideoProducerPIEViewport::~FVideoProducerPIEViewport()
	{
		if (!IsEngineExitRequested())
		{
			UGameViewportClient::OnViewportRendered().Remove(DelegateHandle);
		}
	}

	void FVideoProducerPIEViewport::OnViewportRendered(FViewport* InViewport)
	{
		if (!InViewport->IsPlayInEditorViewport())
		{
			return;
		}

		const FTextureRHIRef& FrameBuffer = InViewport->GetRenderTargetTexture();
		if (!FrameBuffer)
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(StreamViewportTextureCommand)
		([&, FrameBuffer](FRHICommandList& RHICmdList) {
			PushFrame(FPixelCaptureInputFrameRHI(FrameBuffer));
		});
	}

	FString FVideoProducerPIEViewport::ToString()
	{
		return TEXT("the PIE Viewport");
	}

} // namespace UE::PixelStreaming2