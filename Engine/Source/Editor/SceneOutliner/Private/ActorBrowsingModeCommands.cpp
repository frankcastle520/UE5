// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorBrowsingModeCommands.h"

#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UICommandInfo.h"
#include "GenericPlatform/GenericApplication.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "ActorBrowsingModeCommands"

void FActorBrowsingModeCommands::RegisterCommands()
{
	UI_COMMAND(Refresh, "Refresh", "Refresh Outliner", EUserInterfaceActionType::Button, FInputChord(EKeys::F5));
}

#undef LOCTEXT_NAMESPACE
