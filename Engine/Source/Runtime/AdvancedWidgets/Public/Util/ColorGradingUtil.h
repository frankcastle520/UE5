// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/ColorGrading/ColorGradingCommon.h"

namespace UE::ColorGrading
{

/** Get the color grading component typically associated with the given display mode and component index. */
EColorGradingComponent ADVANCEDWIDGETS_API GetColorGradingComponent(EColorGradingColorDisplayMode DisplayMode, int Index);

}
