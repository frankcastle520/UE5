// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "Memory/SharedBuffer.h"

namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGEIMPORT_API FImportImage
		{
			virtual ~FImportImage() = default;
			FImportImage() = default;
			FImportImage(FImportImage&&) = default;
			FImportImage& operator=(FImportImage&&) = default;

			FImportImage(const FImportImage&) = delete;
			FImportImage& operator=(const FImportImage&) = delete;

			FUniqueBuffer RawData;

			/** Which compression format (if any) that is applied to RawData */
			ETextureSourceCompressionFormat RawDataCompressionFormat = TSCF_None;

			ETextureSourceFormat Format = TSF_Invalid;
			TextureCompressionSettings CompressionSettings = TC_Default;
			int32 NumMips = 0;
			int32 SizeX = 0;
			int32 SizeY = 0;
			bool bSRGB = true;
			TOptional<TextureMipGenSettings> MipGenSettings;

			void Init2DWithParams(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, bool bInSRGB, bool bShouldAllocateRawData = true);
			void Init2DWithParams(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, bool bInSRGB, bool bShouldAllocateRawData = true);

			virtual int64 GetMipSize(int32 InMipIndex) const;
			virtual int64 ComputeBufferSize() const;

			TArrayView64<uint8> GetArrayViewOfRawData();
			virtual bool IsValid() const;
		};

	}//ns Interchange
}//ns UE


