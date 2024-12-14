// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneSectionTimingParameters.h"
#include "Evaluation/MovieSceneSequenceTransform.h"
#include "Misc/FrameRate.h"
#include "Variants/MovieSceneTimeWarpVariantPayloads.h"


FMovieSceneSequenceTransform FMovieSceneSectionTimingParametersSeconds::MakeTransform(const FFrameRate& OuterFrameRate, const TRange<FFrameNumber>& OuterRange, double SourceDuration, double InnerPlayRate) const
{
	FMovieSceneSequenceTransform Result;

	check(OuterRange.HasLowerBound());

	if (SourceDuration <= 0)
	{
		// Zero source duration is handled by zero play rate (always evaluate time zero)
		Result.Add(0, FMovieSceneTimeWarpVariant(0.0));
		return Result;
	}

	// ----------------------------------------------------------------------------
	// First things first, subtract the section start bound
	Result.Add(FMovieSceneTimeTransform(-OuterRange.GetLowerBoundValue()));

	// ----------------------------------------------------------------------------
	// Time warp
	Result.Add(0, PlayRate.ShallowCopy());

	// ----------------------------------------------------------------------------
	// FrameRate conversion to seconds
	FMovieSceneTimeWarpVariant FrameRate;
	FrameRate.Set(FMovieSceneTimeWarpFrameRate{ OuterFrameRate });
	Result.Add(0, MoveTemp(FrameRate));

	const double StartTime   = InnerStartOffset;
	const double EndTime     = SourceDuration - InnerEndOffset;
	const double Duration    = EndTime - StartTime;
	double StartOffset = InnerStartOffset + FirstLoopStartOffset;

	// Accomodate negative play rates by playing from the end of the clip
	if (PlayRate.GetType() == EMovieSceneTimeWarpType::FixedPlayRate && PlayRate.AsFixedPlayRate() < 0.0)
	{
		StartOffset += Duration;
	}

	// Start offset
	if (!FMath::IsNearlyZero(StartOffset))
	{
		Result.Add(FMovieSceneTimeTransform(FFrameTime::FromDecimal(StartOffset)));
	}

	// ----------------------------------------------------------------------------
	// Looping or clamping
	if (bLoop)
	{
		// Loop
		FMovieSceneTimeWarpVariant Loop;
		Loop.Set(FMovieSceneTimeWarpLoopFloat{ static_cast<float>(Duration) });
		Result.Add(FFrameTime::FromDecimal(-StartTime), MoveTemp(Loop));
	}
	else if (bClamp)
	{
		// Clamp
		FMovieSceneTimeWarpVariant Clamp;
		Clamp.Set(FMovieSceneTimeWarpClampFloat{ static_cast<float>(Duration) });
		Result.Add(FFrameTime::FromDecimal(-StartTime), MoveTemp(Clamp));
	}

	// ----------------------------------------------------------------------------
	// Reverse
	if (bReverse)
	{
		Result.Add(FMovieSceneTimeTransform(FFrameTime::FromDecimal(Duration), -1.f));
	}

	return Result;
}

FMovieSceneSequenceTransform FMovieSceneSectionTimingParametersFrames::MakeTransform(const FFrameRate& OuterFrameRate, const TRange<FFrameNumber>& OuterRange, const FFrameRate& InnerFrameRate, const TRange<FFrameNumber>& InnerRange) const
{
	FMovieSceneSequenceTransform Result;

	check(OuterRange.HasLowerBound());
	check(InnerRange.HasLowerBound() && InnerRange.HasUpperBound());

	// ----------------------------------------------------------------------------
	// First things first, subtract the section start bound
	Result.Add(FMovieSceneTimeTransform(-OuterRange.GetLowerBoundValue()));

	// ----------------------------------------------------------------------------
	// Time warp
	Result.Add(0, PlayRate.ShallowCopy());

	// ----------------------------------------------------------------------------
	// FrameRate conversion
	if (InnerFrameRate != OuterFrameRate)
	{
		FMovieSceneTimeWarpVariant FrameRate;
		FrameRate.Set(FMovieSceneTimeWarpFrameRate{ OuterFrameRate / InnerFrameRate });
		Result.Add(0, MoveTemp(FrameRate));
	}

	FFrameNumber StartTime = InnerRange.GetLowerBoundValue() + InnerStartOffset;
	FFrameNumber EndTime   = InnerRange.GetUpperBoundValue() - InnerEndOffset;
	FFrameNumber Duration  = EndTime - StartTime;

	FFrameNumber LoopOffset(bLoop ? FirstLoopStartOffset.Value : 0);

	FFrameNumber NegativeRateOffset = 0;
	if (PlayRate.GetType() == EMovieSceneTimeWarpType::FixedPlayRate && PlayRate.AsFixedPlayRate() < 0.0)
	{
		NegativeRateOffset = Duration;
	}

	// Start offset
	Result.Add(FMovieSceneTimeTransform(StartTime + LoopOffset + NegativeRateOffset));

	// ----------------------------------------------------------------------------
	// Looping or clamping
	if (bLoop)
	{
		// Loop
		FMovieSceneTimeWarpVariant Loop;
		Loop.Set(FMovieSceneTimeWarpLoop{ Duration });
		Result.Add(-StartTime, MoveTemp(Loop));
	}
	else if (bClamp)
	{
		// Clamp
		FMovieSceneTimeWarpVariant Clamp;
		Clamp.Set(FMovieSceneTimeWarpClamp{ Duration });
		Result.Add(-StartTime, MoveTemp(Clamp));
	}

	// ----------------------------------------------------------------------------
	// Reverse
	if (bReverse)
	{
		Result.Add(FMovieSceneTimeTransform(Duration, -1.f));
	}

	return Result;
}