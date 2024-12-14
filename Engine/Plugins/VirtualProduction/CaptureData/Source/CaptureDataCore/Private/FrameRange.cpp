// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameRange.h"

bool FFrameRange::operator==(const FFrameRange& InOther) const
{
	return (Name == InOther.Name && StartFrame == InOther.StartFrame && EndFrame == InOther.EndFrame);
}

bool FFrameRange::ContainsFrame(int32 InFrame, const TArray<FFrameRange>& InFrameRangeArray)
{
	for (int32 Index = 0; Index < InFrameRangeArray.Num(); ++Index)
	{
		const FFrameRange& FrameRange = InFrameRangeArray[Index];

		if ((FrameRange.StartFrame >= 0 || FrameRange.EndFrame >= 0) &&
			(FrameRange.StartFrame < 0 || InFrame >= FrameRange.StartFrame) &&
			(FrameRange.EndFrame < 0 || InFrame <= FrameRange.EndFrame))
		{
			return true;
		}
	}

	return false;
}
