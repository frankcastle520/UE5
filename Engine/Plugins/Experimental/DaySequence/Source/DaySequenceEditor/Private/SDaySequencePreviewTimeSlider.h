// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDaySequenceActorPreview;

class SDaySequencePreviewTimeSlider : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDaySequencePreviewTimeSlider) {}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FDaySequenceActorPreview* DaySequenceActorPreview = nullptr;
};

