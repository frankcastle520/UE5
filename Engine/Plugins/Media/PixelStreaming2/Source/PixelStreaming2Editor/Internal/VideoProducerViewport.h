// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoProducer.h"
#include "IPixelStreaming2Streamer.h"
#include "Delegates/IDelegateInstance.h"
#include "UnrealClient.h"

namespace UE::EditorPixelStreaming2
{

	using namespace UE::PixelStreaming2;

	/**
	 * Use this if you want to send the UE primary scene viewport as video input - will only work in editor.
	 */
	class PIXELSTREAMING2EDITOR_API FVideoProducerViewport : public FVideoProducer
	{
	public:
		static TSharedPtr<FVideoProducerViewport> Create(TSharedPtr<IPixelStreaming2Streamer> InAssociatedStreamer);
		virtual ~FVideoProducerViewport();

		virtual FString ToString() override;

	private:
		FVideoProducerViewport() = default;

		bool FilterWindow(SWindow& Window);
		void OnWindowRendered(SWindow& Window, void* Resource);
		void OnPIEViewportRendered(FViewport* InViewport);
		void SubmitViewport(FViewport* InViewport);

		FDelegateHandle DelegateHandle;
		FDelegateHandle PIEDelegateHandle;

		FName TargetViewportType = FName(FString(TEXT("SceneViewport")));

		TWeakPtr<IPixelStreaming2Streamer> AssociatedStreamer;
	};

} // namespace UE::EditorPixelStreaming2
