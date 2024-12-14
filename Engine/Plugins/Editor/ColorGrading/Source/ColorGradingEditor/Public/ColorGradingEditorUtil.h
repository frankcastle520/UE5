// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"

namespace ColorGradingEditorUtil
{

/**
 * Make a button that opens the Color Grading editor.
 * @param bWrapInBox If true, wrap the button in a box that centers it and adds standard padding for use in the Details panel.
 */
COLORGRADINGEDITOR_API TSharedRef<SWidget> MakeColorGradingLaunchButton(bool bWrapInBox = true);

}
