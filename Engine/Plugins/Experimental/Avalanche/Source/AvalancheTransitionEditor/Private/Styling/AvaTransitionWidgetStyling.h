// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Text/SlateWidgetRun.h"

class ISlateStyle;
struct FTextRunInfo;

struct FAvaTransitionWidgetStyling
{
	static FSlateWidgetRun::FWidgetRunInfo CreateOperandWidget(const FTextRunInfo& InRunInfo, const ISlateStyle* InStyle);
};
