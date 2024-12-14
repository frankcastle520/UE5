// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "PixWinPluginCommands.h"

#define LOCTEXT_NAMESPACE "PixWinPlugin"

void FPixWinPluginCommands::RegisterCommands()
{
	UI_COMMAND(CaptureFrameCommand, "Capture Frame", "Captures the next frame and launches the PIX UI if not attached (Alt+F12)", EUserInterfaceActionType::Button, FInputChord(EKeys::F12, EModifierKey::Alt));
}

#undef LOCTEXT_NAMESPACE

#endif //WITH_EDITOR
