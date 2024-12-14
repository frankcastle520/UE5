// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoProducer.h"

namespace UE::PixelStreaming2
{

	TSharedPtr<FVideoProducer> FVideoProducer::Create()
	{
		return TSharedPtr<FVideoProducer>(new FVideoProducer());
	}

	void FVideoProducer::PushFrame(const IPixelCaptureInputFrame& InputFrame)
	{
		OnFramePushed.Broadcast(InputFrame);
	}

	FString FVideoProducer::ToString()
	{
		return TEXT("The default video producer - override me");
	}

} // namespace UE::PixelStreaming2