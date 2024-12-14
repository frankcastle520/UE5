// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterDetailsCommands.h"

#define LOCTEXT_NAMESPACE "DisplayClusterDetails"

void FDisplayClusterDetailsCommands::RegisterCommands()
{
	UI_COMMAND(OpenDetailsDrawer, "Open In-Camera VFX Drawer", "Opens the In-Camera VFX details drawer from the status bar", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::SpaceBar));
}

#undef LOCTEXT_NAMESPACE
