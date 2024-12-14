// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoProducerRenderTarget.h"

#include "Engine/TextureRenderTarget2D.h"
#include "PixelCaptureInputFrameRHI.h"
#include "RHI.h"
#include "TextureResource.h"
#include "Widgets/SWindow.h"

namespace UE::PixelStreaming2
{

	TSharedPtr<FVideoProducerRenderTarget> FVideoProducerRenderTarget::Create(UTextureRenderTarget2D* Target)
	{
		return TSharedPtr<FVideoProducerRenderTarget>(new FVideoProducerRenderTarget(Target));
	}

	FVideoProducerRenderTarget::~FVideoProducerRenderTarget()
	{
		FCoreDelegates::OnEndFrameRT.Remove(DelegateHandle);
	}

	FVideoProducerRenderTarget::FVideoProducerRenderTarget(UTextureRenderTarget2D* InTarget)
		: Target(InTarget)
		, DelegateHandle(FCoreDelegates::OnEndFrameRT.AddRaw(this, &FVideoProducerRenderTarget::OnEndFrameRenderThread))
	{
	}

	void FVideoProducerRenderTarget::OnEndFrameRenderThread()
	{
		if (Target)
		{
			if (FTextureRHIRef Texture = Target->GetResource()->GetTexture2DRHI())
			{
				PushFrame(FPixelCaptureInputFrameRHI(Texture));
			}
		}
	}

	FString FVideoProducerRenderTarget::ToString()
	{
		return TEXT("a Render Target");
	}

} // namespace UE::PixelStreaming2