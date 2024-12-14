// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/DoubleChannelCurveModel.h"

struct FMovieSceneTimeWarpChannel;

class FTimeWarpChannelCurveModel : public FDoubleChannelCurveModel
{
public:

	FTimeWarpChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneTimeWarpChannel> InChannel, UMovieSceneSection* InOwningSection, UObject* InOwningObject, TWeakPtr<ISequencer> InWeakSequencer);

	void SetCurveAttributes(const FCurveAttributes& InCurveAttributes) override;

	void GetCurveAttributes(FCurveAttributes& OutCurveAttributes) const override;

	void AllocateAxes(FCurveEditor* InCurveEditor, TSharedPtr<FCurveEditorAxis>& OutHorizontalAxis, TSharedPtr<FCurveEditorAxis>& OutVerticalAxis) const override;

	void MakeChildCurves(TArray<TUniquePtr<FCurveModel>>& OutChildCurves) const override;
};