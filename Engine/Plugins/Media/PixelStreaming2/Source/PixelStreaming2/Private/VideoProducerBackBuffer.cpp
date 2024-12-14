// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoProducerBackBuffer.h"

#include "Framework/Application/SlateApplication.h"
#include "Logging.h"
#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureInputFrameRHI.h"
#include "RHI.h"
#include "UtilsCommon.h"
#include "Widgets/SWindow.h"


namespace UE::PixelStreaming2
{

	TSharedPtr<FVideoProducerBackBuffer> FVideoProducerBackBuffer::Create()
	{
		// this was added to fix packaging
		if (!FSlateApplication::IsInitialized())
		{
			return nullptr;
		}

		TSharedPtr<FVideoProducerBackBuffer> NewInput = TSharedPtr<FVideoProducerBackBuffer>(new FVideoProducerBackBuffer());
		TWeakPtr<FVideoProducerBackBuffer>	 WeakInput = NewInput;

		// Set up the callback on the game thread since FSlateApplication::Get() can only be used there
		UE::PixelStreaming2::DoOnGameThread([WeakInput]() {
			if (TSharedPtr<FVideoProducerBackBuffer> Input = WeakInput.Pin())
			{
				FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
				Input->DelegateHandle = Renderer->OnBackBufferReadyToPresent().AddSP(Input.ToSharedRef(), &FVideoProducerBackBuffer::OnBackBufferReady);
			}
		});

		return NewInput;
	}

	FVideoProducerBackBuffer::~FVideoProducerBackBuffer()
	{
		if (!IsEngineExitRequested())
		{
			UE::PixelStreaming2::DoOnGameThread([HandleCopy = DelegateHandle]() {
				FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(HandleCopy);
			});
		}
	}

	void FVideoProducerBackBuffer::OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& FrameBuffer)
	{
		PushFrame(FPixelCaptureInputFrameRHI(FrameBuffer));
	}

	FString FVideoProducerBackBuffer::ToString()
	{
		return TEXT("the Back Buffer");
	}

} // namespace UE::PixelStreaming2