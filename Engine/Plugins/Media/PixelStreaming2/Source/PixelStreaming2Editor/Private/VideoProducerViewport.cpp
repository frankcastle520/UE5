// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoProducerViewport.h"

#include "Async/Async.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "IPixelStreaming2Module.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureCapturerRHI.h"
#include "PixelCaptureInputFrameRHI.h"
#include "RenderingThread.h"
#include "SLevelViewport.h"
#include "UnrealClient.h"
#include "Utils.h"

namespace UE::EditorPixelStreaming2
{

	bool IsPIESessionRunning()
	{
		return GEditor && GEditor->PlayWorld && GEditor->PlayWorld->WorldType == EWorldType::PIE;
	}

	TSharedPtr<FVideoProducerViewport> FVideoProducerViewport::Create(TSharedPtr<IPixelStreaming2Streamer> InAssociatedStreamer)
	{
		TSharedPtr<FVideoProducerViewport> NewInput = TSharedPtr<FVideoProducerViewport>(new FVideoProducerViewport());
		NewInput->AssociatedStreamer = InAssociatedStreamer;
		TWeakPtr<FVideoProducerViewport> WeakInput = NewInput;

		// Set up the callback on the game thread since FSlateApplication::Get() can only be used there
		AsyncTask(ENamedThreads::GameThread, [WeakInput]() {
			if (TSharedPtr<FVideoProducerViewport> Input = WeakInput.Pin())
			{
				// Bind to GameViewport delegate for PIE sessions
				Input->PIEDelegateHandle = UGameViewportClient::OnViewportRendered().AddSP(Input.ToSharedRef(),
					&FVideoProducerViewport::OnPIEViewportRendered);

				// For non-PIE cases (just using level editor) then bind to slate window rendered
				FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
				if (!Renderer)
				{
					return;
				}

				Input->DelegateHandle = Renderer->OnSlateWindowRendered().AddSP(Input.ToSharedRef(),
					&FVideoProducerViewport::OnWindowRendered);
			}
		});

		return NewInput;
	}

	FVideoProducerViewport::~FVideoProducerViewport()
	{
		if (!IsEngineExitRequested())
		{
			AsyncTask(ENamedThreads::GameThread, [HandleCopy = DelegateHandle, PIEHandleCopy = PIEDelegateHandle]() {
				UGameViewportClient::OnViewportRendered().Remove(PIEHandleCopy);
				FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
				if (Renderer)
				{
					Renderer->OnSlateWindowRendered().Remove(HandleCopy);
				}
			});
		}
	}

	bool FVideoProducerViewport::FilterWindow(SWindow& InWindow)
	{
		TSharedPtr<IPixelStreaming2Streamer> Streamer = AssociatedStreamer.Pin();
		if (!Streamer || !Streamer->IsStreaming())
		{
			return false;
		}

		TSharedPtr<SWindow> TargetWindow;
		if (TSharedPtr<IPixelStreaming2InputHandler> InputHandler = Streamer->GetInputHandler().Pin())
		{
			TargetWindow = InputHandler->GetTargetWindow().Pin();
		}

		if (!TargetWindow)
		{
			return false;
		}

		// Check if the window we were passed is our target window.
		if (TargetWindow.Get() != &InWindow)
		{
			return false;
		}

		return true;
	}

	void FVideoProducerViewport::OnWindowRendered(SWindow& InWindow, void* InResource)
	{
		// If there is a PIE session running then don't render level viewport
		if (IsPIESessionRunning())
		{
			return;
		}

		if (!FilterWindow(InWindow))
		{
			return;
		}

		TSharedPtr<IPixelStreaming2Streamer> Streamer = AssociatedStreamer.Pin();
		if (!Streamer || !Streamer->IsStreaming())
		{
			return;
		}

		FLevelEditorModule&		   LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
		if (!ActiveLevelViewport.IsValid())
		{
			return;
		}

		FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
		FViewport*					TargetViewport = LevelViewportClient.Viewport;

		SubmitViewport(TargetViewport);
	}

	void FVideoProducerViewport::OnPIEViewportRendered(FViewport* InViewport)
	{
		// If gameclient viewport is rendered and it is not PIE we are not interested
		if (!IsPIESessionRunning() || !InViewport || !InViewport->IsPlayInEditorViewport())
		{
			return;
		}

		SubmitViewport(InViewport);
	}

	void FVideoProducerViewport::SubmitViewport(FViewport* InViewport)
	{
		if (!InViewport)
		{
			return;
		}

		const FTextureRHIRef& FrameBuffer = InViewport->GetRenderTargetTexture();
		if (!FrameBuffer)
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(StreamViewportTextureCommand)
		([this, FrameBuffer](FRHICommandList& RHICmdList) {
			PushFrame(FPixelCaptureInputFrameRHI(FrameBuffer));
		});
	}

	FString FVideoProducerViewport::ToString()
	{
		return TEXT("the Target Viewport");
	}

} // namespace UE::EditorPixelStreaming2