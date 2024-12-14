// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Video/DependencyDescriptor.h"

struct AVCODECSCORE_API FCodecBufferUsage
{
	int32 Id = 0;
	bool  bReferenced = false;
	bool  bUpdated = false;
};

class AVCODECSCORE_API FGenericFrameInfo : public FFrameDependencyTemplate
{
public:
	FGenericFrameInfo();

	friend bool operator==(const FGenericFrameInfo& Lhs, const FGenericFrameInfo& Rhs)
	{
		return Lhs.SpatialId == Rhs.SpatialId
			&& Lhs.TemporalId == Rhs.TemporalId
			&& Lhs.DecodeTargetIndications == Rhs.DecodeTargetIndications
			&& Lhs.FrameDiffs == Rhs.FrameDiffs
			&& Lhs.ChainDiffs == Rhs.ChainDiffs;
	}

	TArray<FCodecBufferUsage> EncoderBuffers;
	TArray<bool>			  PartOfChain;
	TArray<bool>			  ActiveDecodeTargets;
};