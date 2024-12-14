// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Video/VideoDecoder.h"

/*
 * Configuration settings for VP8 decoders.
 */
struct AVCODECSCORE_API FVideoDecoderConfigVP8 : public FVideoDecoderConfig
{
	int32 NumberOfCores = 0;

	FVideoDecoderConfigVP8(EAVPreset Preset = EAVPreset::Default)
		: FVideoDecoderConfig(Preset)
	{
	}
};

DECLARE_TYPEID(FVideoDecoderConfigVP8, AVCODECSCORE_API);
