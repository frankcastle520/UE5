// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/MovieSceneTimeWarpChannel.h"
#include "Channels/MovieScenePiecewiseCurve.h"
#include "Variants/MovieSceneTimeWarpGetter.h"
#include "MovieScenePlayRateCurve.generated.h"


/**
 * A time-warp defined as a play rate curve.
 * Time remapping is computed using the integral of the play rate curve.
 */
UCLASS(MinimalAPI, DisplayName="Play Rate Curve")
class UMovieScenePlayRateCurve : public UMovieSceneTimeWarpGetter
{
public:

	GENERATED_BODY()

	UMovieScenePlayRateCurve();

	/* Begin UMovieSceneTimeWarpGetter Implementation */
	FFrameTime RemapTime(FFrameTime In) const override;
	TOptional<FFrameTime> InverseRemapTimeCycled(FFrameTime InValue, FFrameTime InTimeHint, const UE::MovieScene::FInverseTransformTimeParams& Params) const override;
	TRange<FFrameTime> ComputeTraversedHull(const TRange<FFrameTime>& Range) const override;
	bool InverseRemapTimeWithinRange(FFrameTime InTime, FFrameTime RangeStart, FFrameTime RangeEnd, const TFunctionRef<bool(FFrameTime)>& VisitorCallback) const override;
	void InitializeDefaults() override;
	EMovieSceneChannelProxyType PopulateChannelProxy(FMovieSceneChannelProxyData& OutProxyData, EAllowTopLevelChannels AllowTopLevel) override;
	bool DeleteChannel(FMovieSceneTimeWarpVariant& OutVariant, FName ChannelName) override;
	void ScaleBy(double UnwarpedScaleFactor) override;
	UE::MovieScene::ETimeWarpChannelDomain GetDomain() const override;
	/* End UMovieSceneTimeWarpGetter Implementation */

#if WITH_EDITOR
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
#endif

	MOVIESCENE_API const UE::MovieScene::FPiecewiseCurve& GetTimeWarpCurve() const;

private:

	void InvalidateTimeWarp();

private:

	/** Curve defined in play-rate space. Does not support cycling. */
	UPROPERTY(EditAnywhere, Category="TimeWarp")
	FMovieSceneTimeWarpChannel PlayRate;

	/** Curve defined in time-warp space as an integral of the PlayRate curve */
	mutable UE::MovieScene::FPiecewiseCurve IntegratedTimeWarp;

	/** false when IntegratedTimeWarp needs to be udpated */
	mutable bool bUpToDate = false;
};