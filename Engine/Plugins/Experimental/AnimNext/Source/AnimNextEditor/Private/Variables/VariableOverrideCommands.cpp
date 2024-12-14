// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariableOverrideCommands.h"

#define LOCTEXT_NAMESPACE "VariableOverrideCommands"

namespace UE::AnimNext
{

FVariableOverrideCommands::FVariableOverrideCommands()
	: TCommands<FVariableOverrideCommands>("AnimNext", LOCTEXT("AnimNextVariableOverrides", "Variable Overrides"), NAME_None, "AnimNextStyle")
{
}

void FVariableOverrideCommands::RegisterCommands()
{
	UI_COMMAND(OverrideVariable, "Override Variable", "Override this variable from its inherited value.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ResetPropertyToDefault, "Reset to Default", "Revert this property to its inherited value, but retain its override.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ClearOverride, "Clear Override", "Revert this property to its inherited value, and clear its override.", EUserInterfaceActionType::Button, FInputChord());
}

}

#undef LOCTEXT_NAMESPACE
