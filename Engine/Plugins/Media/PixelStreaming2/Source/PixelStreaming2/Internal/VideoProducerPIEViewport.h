// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "VideoProducer.h"

class FViewport;

namespace UE::PixelStreaming2
{
	/**
	 * An extension of the back buffer input that can handle PIE sessions. Primarily to be used in blueprints
	 */
	class PIXELSTREAMING2_API FVideoProducerPIEViewport : public FVideoProducer
	{
	public:
		static TSharedPtr<FVideoProducerPIEViewport> Create();
		virtual ~FVideoProducerPIEViewport();

		virtual FString ToString() override;

	private:
		FVideoProducerPIEViewport() = default;

		void OnViewportRendered(FViewport* InViewport);

		FDelegateHandle DelegateHandle;
	};

} // namespace UE::PixelStreaming2