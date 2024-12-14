// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameRate.h"
#include "Video/Encoders/VideoBitrateAllocation.h"

class AVCODECSCORE_API FVideoBitrateAllocationParameters
{
public:
	FVideoBitrateAllocationParameters(uint32 TotalBitrateBps, FFrameRate Framerate)
		: TotalBitrateBps(TotalBitrateBps)
		, StableBitrateBps(TotalBitrateBps)
		, Framerate(Framerate)
	{
	}
	FVideoBitrateAllocationParameters(uint32 TotalBitrateBps, uint32 StableBitrateBps, FFrameRate Framerate)
		: TotalBitrateBps(TotalBitrateBps)
		, StableBitrateBps(StableBitrateBps)
		, Framerate(Framerate)
	{
	}

	uint32 TotalBitrateBps;
	uint32 StableBitrateBps;
	FFrameRate Framerate;
};

class AVCODECSCORE_API FVideoBitrateAllocator
{
public:
	FVideoBitrateAllocator() = default;
	~FVideoBitrateAllocator() = default;

	virtual FVideoBitrateAllocation GetAllocation(uint32 TotalBitrateBps, FFrameRate Framerate);

	virtual FVideoBitrateAllocation Allocate(FVideoBitrateAllocationParameters Parameters);
};