// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Texture2DDynamic.h"
#include "IPixelStreaming2VideoConsumer.h"

#include "PixelStreaming2MediaTexture.generated.h"

namespace UE::PixelStreaming2
{
	class FPixelStreaming2MediaTextureResource;
} // namespace UE::PixelStreaming2

/**
 * A Texture Object that can be used in materials etc. that takes updates from webrtc frames.
 */
UCLASS(NotBlueprintType, NotBlueprintable, HideDropdown, HideCategories = (ImportSettings, Compression, Texture, Adjustments, Compositing, LevelOfDetail, Object), META = (DisplayName = "PixelStreaming Media Texture"))
class PIXELSTREAMING2_API UPixelStreaming2MediaTexture : public UTexture2DDynamic, public IPixelStreaming2VideoConsumer
{
	GENERATED_UCLASS_BODY()

protected:
	// UObject overrides.
	virtual void BeginDestroy() override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;

	// UTexture implementation
	virtual FTextureResource* CreateResource() override;

	// IPixelStreamingVideoConsumer implementation
	virtual void ConsumeFrame(FTextureRHIRef Frame) override;
	virtual void OnConsumerAdded() override {}
	virtual void OnConsumerRemoved() override {}

private:
	void InitializeResources();

	// updates the internal texture resource after each frame.
	void UpdateTextureReference(FRHICommandList& RHICmdList, FTextureRHIRef Reference);

	FCriticalSection RenderSyncContext;
	// NOTE: This type has to be a raw ptr because UE will manage the lifetime of it
	UE::PixelStreaming2::FPixelStreaming2MediaTextureResource* CurrentResource;
};
