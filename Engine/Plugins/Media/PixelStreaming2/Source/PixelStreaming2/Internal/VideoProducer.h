// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "IPixelStreaming2VideoProducer.h"
#include "Templates/SharedPointer.h"

namespace UE::PixelStreaming2
{
	class PIXELSTREAMING2_API FVideoProducer : public IPixelStreaming2VideoProducer
	{
	public:
		static TSharedPtr<FVideoProducer> Create();
		virtual ~FVideoProducer() = default;

		DECLARE_EVENT_OneParam(FVideoProducer, FOnFramePushed, const IPixelCaptureInputFrame&);
		FOnFramePushed OnFramePushed;

		virtual void PushFrame(const IPixelCaptureInputFrame& InputFrame) override;

		virtual FString ToString() override;

	protected:
		FVideoProducer() = default;
	};

} // namespace UE::PixelStreaming2