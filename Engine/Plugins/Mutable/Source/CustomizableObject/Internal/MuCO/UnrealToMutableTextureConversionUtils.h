// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Engine/Texture.h"

#if WITH_EDITOR
struct CUSTOMIZABLEOBJECT_API FMutableSourceTextureData
{
	FMutableSourceTextureData() = default;
	
	FMutableSourceTextureData(const UTexture& Texture);
	
	FTextureSource& GetSource();
	
	bool GetFlipGreenChannel() const;
	
	bool HasAlphaChannel() const;

	bool GetCompressionForceAlpha() const;

	bool IsNormalComposite() const;

private:
	FTextureSource Source;
	bool bFlipGreenChannel = false;
	bool bHasAlphaChannel = false;
	bool bCompressionForceAlpha = false;
	bool bIsNormalComposite = false;
};


namespace mu
{
	class Image;
}

// Forward declarations
class UTexture2D;

enum class EUnrealToMutableConversionError 
{
    Success,
    UnsupportedFormat,
    CompositeImageDimensionMismatch,
    CompositeUnsupportedFormat,
    Unknown
};

CUSTOMIZABLEOBJECT_API EUnrealToMutableConversionError ConvertTextureUnrealSourceToMutable(mu::Image* OutResult, FMutableSourceTextureData&, uint8 MipmapsToSkip);

#endif
