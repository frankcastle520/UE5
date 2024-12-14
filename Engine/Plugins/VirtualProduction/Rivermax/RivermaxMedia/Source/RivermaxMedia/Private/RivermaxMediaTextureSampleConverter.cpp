// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaTextureSampleConverter.h"

namespace UE::RivermaxMedia
{
	uint32 FRivermaxMediaTextureSampleConverter::GetConverterInfoFlags() const
	{
		if (TSharedPtr<FMediaIOCoreTextureSampleBase> ProxySampleSharedPtr = JITRProxySample.Pin())
		{
			if (ProxySampleSharedPtr->GetEncodingType() != UE::Color::EEncoding::None || ProxySampleSharedPtr->GetColorSpaceType() != UE::Color::EColorSpace::None)
			{
				return ConverterInfoFlags_WillCreateOutputTexture | ConverterInfoFlags_PreprocessOnly;
			}
		}
		return ConverterInfoFlags_WillCreateOutputTexture;
	}
}