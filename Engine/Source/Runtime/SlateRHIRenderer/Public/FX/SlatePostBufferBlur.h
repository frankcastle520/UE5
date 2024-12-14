// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FX/SlateRHIPostBufferProcessor.h"
#include "SlatePostBufferBlur.generated.h"

/**
 * Proxy for post buffer processor that the renderthread uses to perform processing
 * This proxy exists because generally speaking usage on UObjects on the renderthread
 * is a race condition due to UObjects being managed / updated by the game thread
 */
class SLATERHIRENDERER_API FSlatePostBufferBlurProxy : public FSlateRHIPostBufferProcessorProxy
{

public:

	//~ Begin FSlateRHIPostBufferProcessorProxy Interface
	virtual void PostProcess_Renderthread(FRDGBuilder& GraphBuilder, const FScreenPassTexture& InputTexture, const FScreenPassTexture& OutputTexture) override;
	virtual void OnUpdateValuesRenderThread() override;
	//~ End FSlateRHIPostBufferProcessorProxy Interface

	/** Blur strength to use when processing, renderthread version actually used to draw. Must be updated via render command except during initialization. */
	float GaussianBlurStrength_RenderThread = 10;

	/** 
	 * Blur strength can be updated from both renderthread during draw and gamethread update. 
	 * Store the last value gamethread provided so we know if we should use the renderthread value or gamethread value. 
	 * We will use the most recently updated one.
	 */
	float GaussianBlurStrengthPreDraw = 10;

protected:

	/** Fence to allow for us to queue only one update per draw command from the gamethread */
	FRenderCommandFence ParamUpdateFence;
};

/**
 * Slate Post Buffer Processor that performs a simple gaussian blur to the backbuffer
 * 
 * Create a new asset deriving from this class to use / modify settings.
 */
UCLASS(Abstract, Blueprintable, CollapseCategories)
class SLATERHIRENDERER_API USlatePostBufferBlur : public USlateRHIPostBufferProcessor
{
	GENERATED_BODY()

public:

	UPROPERTY(interp, BlueprintReadWrite, Category = "GaussianBlur")
	float GaussianBlurStrength = 10;

public:

	USlatePostBufferBlur();
	virtual ~USlatePostBufferBlur() override;

	virtual TSharedPtr<FSlateRHIPostBufferProcessorProxy> GetRenderThreadProxy();

private:

	TSharedPtr<FSlateRHIPostBufferProcessorProxy> RenderThreadProxy;
};