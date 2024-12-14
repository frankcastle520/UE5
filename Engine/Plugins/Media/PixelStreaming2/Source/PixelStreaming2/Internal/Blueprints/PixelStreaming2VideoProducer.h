// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/TextureRenderTarget2D.h"
#include "IPixelStreaming2VideoProducer.h"

#include "PixelStreaming2VideoProducer.generated.h"

/**
 * The base video producer blueprint class
 */
UCLASS(NotBlueprintType, NotBlueprintable, Category = "PixelStreaming2", META = (DisplayName = "Video Producer"))
class PIXELSTREAMING2_API UPixelStreaming2VideoProducer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	virtual TSharedPtr<IPixelStreaming2VideoProducer> GetVideoProducer() { return VideoProducer; }

protected:
	TSharedPtr<IPixelStreaming2VideoProducer> VideoProducer;
};

/**
 * A video producer that streams the Unreal Engine's back buffer
 */
UCLASS(NotBlueprintType, NotBlueprintable, Category = "PixelStreaming2", META = (DisplayName = "Back Buffer Video Producer"))
class PIXELSTREAMING2_API UPixelStreaming2VideoProducerBackBuffer : public UPixelStreaming2VideoProducer
{
	GENERATED_UCLASS_BODY()

public:
	virtual TSharedPtr<IPixelStreaming2VideoProducer> GetVideoProducer() override;
};

/**
 * A video producer that streams the Unreal Engine's back buffer but uses the MediaIO Framework to capture the frame rather than Pixel Capture
 */
UCLASS(NotBlueprintType, NotBlueprintable, Category = "PixelStreaming2", META = (DisplayName = "Media Capture Video Producer"))
class PIXELSTREAMING2_API UPixelStreaming2VideoProducerMediaCapture : public UPixelStreaming2VideoProducer
{
	GENERATED_UCLASS_BODY()

public:
	virtual TSharedPtr<IPixelStreaming2VideoProducer> GetVideoProducer() override;
};

/**
 * A video producer that streams the specified render target
 */
UCLASS(NotBlueprintType, NotBlueprintable, Category = "PixelStreaming2", META = (DisplayName = "Render Target Video Producer"))
class PIXELSTREAMING2_API UPixelStreaming2VideoProducerRenderTarget : public UPixelStreaming2VideoProducer
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Video, AssetRegistrySearchable)
	TObjectPtr<UTextureRenderTarget2D> Target;

	virtual TSharedPtr<IPixelStreaming2VideoProducer> GetVideoProducer() override;
};