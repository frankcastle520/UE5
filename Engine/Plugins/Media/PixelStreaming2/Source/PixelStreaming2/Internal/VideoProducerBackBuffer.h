// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoProducer.h"
#include "Widgets/SWindow.h"
#include "RHI.h"
#include "Delegates/IDelegateInstance.h"

namespace UE::PixelStreaming2
{
	/**
	 * Use this if you want to send the contents of the UE backbuffer.
	 */
	class PIXELSTREAMING2_API FVideoProducerBackBuffer : public FVideoProducer
	{
	public:
		static TSharedPtr<FVideoProducerBackBuffer> Create();
		virtual ~FVideoProducerBackBuffer();

		virtual FString ToString() override;

	private:
		FVideoProducerBackBuffer() = default;

		void OnBackBufferReady(SWindow& SlateWindow, const FTextureRHIRef& FrameBuffer);

		FDelegateHandle DelegateHandle;
	};

} // namespace UE::PixelStreaming2