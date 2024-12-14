// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "VideoProducer.h"

class UTextureRenderTarget2D;

namespace UE::PixelStreaming2
{
	/**
	 * Use this if you want to send the contents of a render target.
	 */
	class PIXELSTREAMING2_API FVideoProducerRenderTarget : public FVideoProducer
	{
	public:
		static TSharedPtr<FVideoProducerRenderTarget> Create(UTextureRenderTarget2D* Target);
		virtual ~FVideoProducerRenderTarget();

		virtual FString ToString() override;

	private:
		FVideoProducerRenderTarget(UTextureRenderTarget2D* InTarget);
		void OnEndFrameRenderThread();

		UTextureRenderTarget2D* Target = nullptr;
		FDelegateHandle			DelegateHandle;
	};

} // namespace UE::PixelStreaming2