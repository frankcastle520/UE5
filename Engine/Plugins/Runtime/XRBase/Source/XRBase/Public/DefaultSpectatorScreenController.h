// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "ISpectatorScreenController.h"
#include "HeadMountedDisplayTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FHeadMountedDisplayBase;

DECLARE_DELEGATE_FiveParams(FSpectatorScreenRenderDelegate, FRHICommandListImmediate& /* RHICmdList */, FTextureRHIRef /* TargetTexture */, FTextureRHIRef /* EyeTexture */, FTextureRHIRef /* OtherTexture */, FVector2D /* WindowSize */);

/** 
 *	Default implementation of spectator screen controller.
 *
 */
class XRBASE_API FDefaultSpectatorScreenController : public ISpectatorScreenController, public TSharedFromThis<FDefaultSpectatorScreenController, ESPMode::ThreadSafe>
{
public:
	FDefaultSpectatorScreenController(FHeadMountedDisplayBase* InHMDDevice);
	virtual ~FDefaultSpectatorScreenController() {}

	// ISpectatorScreenController
	virtual ESpectatorScreenMode GetSpectatorScreenMode() const override;
	virtual void SetSpectatorScreenMode(ESpectatorScreenMode Mode) override;
	virtual void SetSpectatorScreenTexture(UTexture* InTexture) override;
	virtual UTexture* GetSpectatorScreenTexture() const override;
	virtual void SetSpectatorScreenModeTexturePlusEyeLayout(const FSpectatorScreenModeTexturePlusEyeLayout& Layout) override;
	virtual void QueueDebugCanvasLayerID(int32 LayerID) override;

	FSpectatorScreenRenderDelegate* GetSpectatorScreenRenderDelegate_RenderThread();


	// Implementation methods called by HMD
	virtual void BeginRenderViewFamily();
	virtual void UpdateSpectatorScreenMode_RenderThread();
	virtual void RenderSpectatorScreen_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* BackBuffer, FTextureRHIRef SrcTexture, FVector2D WindowSize);
	virtual void RenderSpectatorScreen_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* BackBuffer, FTextureRHIRef SrcTexture, FTextureRHIRef LayersTexture, FVector2D WindowSize);

protected:
	friend struct FRHISetSpectatorScreenTexture;
	virtual void SetSpectatorScreenTextureRenderCommand(UTexture* SrcTexture);
	virtual void SetSpectatorScreenTexture_RenderThread(FTextureRHIRef& InTexture);

	friend struct FRHISetSpectatorScreenModeTexturePlusEyeLayout;
	virtual void SetSpectatorScreenModeTexturePlusEyeLayoutRenderCommand(const FSpectatorScreenModeTexturePlusEyeLayout& Layout);
	virtual void SetSpectatorScreenModeTexturePlusEyeLayout_RenderThread(const FSpectatorScreenModeTexturePlusEyeLayout& Layout);

	virtual FIntRect GetFullFlatEyeRect_RenderThread(FTextureRHIRef EyeTexture);

	virtual void RenderSpectatorModeSingleEyeLetterboxed(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);
	virtual void RenderSpectatorModeUndistorted(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);
	virtual void RenderSpectatorModeDistorted(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);
	virtual void RenderSpectatorModeSingleEye(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);
	virtual void RenderSpectatorModeTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);
	virtual void RenderSpectatorModeMirrorAndTexture(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);
	virtual void RenderSpectatorModeSingleEyeCroppedToFill(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, FTextureRHIRef EyeTexture, FTextureRHIRef OtherTexture, FVector2D WindowSize);

	virtual FRHITexture* GetFallbackRHITexture() const;

	mutable FCriticalSection NewSpectatorScreenModeLock;
	ESpectatorScreenMode NewSpectatorScreenMode = ESpectatorScreenMode::SingleEyeCroppedToFill;
	TWeakObjectPtr<UTexture> SpectatorScreenTexture;

	ESpectatorScreenMode SpectatorScreenMode_RenderThread = ESpectatorScreenMode::Disabled;
	FTextureRHIRef SpectatorScreenTexture_RenderThread;
	FSpectatorScreenModeTexturePlusEyeLayout SpectatorScreenModeTexturePlusEyeLayout_RenderThread;
	FSpectatorScreenRenderDelegate SpectatorScreenDelegate_RenderThread;
	TArray<int32> DebugCanvasLayerIDs;

	class XRBASE_API Helpers
	{
	public:
		static FIntRect GetEyeCroppedToFitRect(FVector2D EyeCenterPoint, const FIntRect& EyeRect, const FIntRect& TargetRect);
		static FIntRect GetLetterboxedDestRect(const FIntRect& SrcRect, const FIntRect& TargetRect);
	};

private:
	void CopyEmulatedLayers(FRHICommandListImmediate& RHICmdList, FTextureRHIRef TargetTexture, const FIntRect SrcRect, const FIntRect DstRect);

	FHeadMountedDisplayBase* HMDDevice;
	// Face locked stereo layers are composited to a single texture which has to be copied over to the spectator screen.
	FTextureRHIRef StereoLayersTexture;
};