// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterColorGradingCommands.h"

#define LOCTEXT_NAMESPACE "DisplayClusterColorGrading"

void FDisplayClusterColorGradingCommands::RegisterCommands()
{
	UI_COMMAND(OpenColorGradingDrawer, "Open Color Grading Drawer", "Opens the color grading drawer from the status bar", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::SpaceBar));
}

#undef LOCTEXT_NAMESPACE
