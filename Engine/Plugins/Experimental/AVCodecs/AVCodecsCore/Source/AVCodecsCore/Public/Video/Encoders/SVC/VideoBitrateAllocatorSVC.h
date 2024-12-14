// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Video/Encoders/VideoBitrateAllocator.h"
#include "Video/VideoEncoder.h"

class AVCODECSCORE_API FVideoBitrateAllocatorSVC : public FVideoBitrateAllocator
{
public:
	// NOTE: Config stores its rates as kbps whereas this class will use bps. Convert to bps everywhere
	explicit FVideoBitrateAllocatorSVC(const FVideoEncoderConfig& Config);
	virtual ~FVideoBitrateAllocatorSVC() = default;

	FVideoBitrateAllocation Allocate(FVideoBitrateAllocationParameters Parameters) override;

	static uint32		  GetMaxBitrate(const FVideoEncoderConfig& Config);
	static uint32		  GetPaddingBitrate(const FVideoEncoderConfig& Config);
	static TArray<uint32> GetLayerStartBitrates(const FVideoEncoderConfig& Config);

private:
	struct FNumLayers
	{
		size_t Spatial = 1;
		size_t Temporal = 1;
	};

	static FNumLayers		GetNumLayers(const FVideoEncoderConfig& Config);
	FVideoBitrateAllocation GetAllocationNormalVideo(uint32 TotalBitrateBps, size_t FirstActiveLayer, size_t NumSpatialLayers) const;

	// Returns the number of layers that are active and have enough bitrate to
	// actually be enabled.
	size_t FindNumEnabledLayers(uint32 TargetRate) const;

	FVideoEncoderConfig	 Config;
	const FNumLayers	 NumLayers;
	const TArray<uint32> CumulativeLayerStartBitrates;
	size_t				 LastActiveLayerCount;
};