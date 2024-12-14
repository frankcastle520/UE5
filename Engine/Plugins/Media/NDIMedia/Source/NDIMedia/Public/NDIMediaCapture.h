// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaCapture.h"
#include "HAL/CriticalSection.h"
#include "Misc/FrameRate.h"
#include "NDIMediaOutput.h"

#include "NDIMediaCapture.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNDIMedia, Log, All);

/**
 * 
 */
UCLASS()
class NDIMEDIA_API UNDIMediaCapture : public UMediaCapture
{
	GENERATED_BODY()

public:
	virtual ~UNDIMediaCapture();	// so we can pimpl.

protected:
	virtual void OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow) override;
	virtual bool InitializeCapture() override;
	virtual bool PostInitializeCaptureViewport(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool PostInitializeCaptureRenderTarget(UTextureRenderTarget2D* InRenderTarget) override;
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override;

private:
	bool StartNewCapture();

private:
	FCriticalSection CaptureInstanceCriticalSection;

	// @note:
	// It is unclear at this point of our design if UNDIMediaCapture should 
	// be public, but because it is, to maximize our options for now, we don't
	// want NDI SDK headers to be public with it, so we use a pimpl for now.

	// Private implementation

	class FNDICaptureInstance;
	FNDICaptureInstance* CaptureInstance = nullptr;
};
