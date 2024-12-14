// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditorCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigEditorCommands"

void FControlRigEditorCommands::RegisterCommands()
{
	UI_COMMAND(ConstructionEvent, "Construction Event", "Enable the construction mode for the rig", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ForwardsSolveEvent, "Forwards Solve", "Run the forwards solve graph", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BackwardsSolveEvent, "Backwards Solve", "Run the backwards solve graph", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BackwardsAndForwardsSolveEvent, "Backwards and Forwards", "Run backwards solve followed by forwards solve", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RequestDirectManipulationPosition, "Request Direct Manipulation for Position", "Request per node direct manipulation on a position", EUserInterfaceActionType::Button, FInputChord(EKeys::W));
	UI_COMMAND(RequestDirectManipulationRotation, "Request Direct Manipulation for Rotation", "Request per node direct manipulation on a rotation", EUserInterfaceActionType::Button, FInputChord(EKeys::E));
	UI_COMMAND(RequestDirectManipulationScale, "Request Direct Manipulation for Scale", "Request per node direct manipulation on a scale", EUserInterfaceActionType::Button, FInputChord(EKeys::R));
	UI_COMMAND(ToggleControlVisibility, "Show Controls", "Toggles the visibility of the controls.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::T));
	UI_COMMAND(ToggleControlsAsOverlay, "Show Controls as Overlay", "If checked controls will be rendered on top of other controls.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleDrawNulls, "Display Nulls", "If checked all nulls are drawn as axes.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleDrawSockets, "Display Sockets", "If checked all sockets are drawn.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleDrawAxesOnSelection, "Display Axes On Selection", "If checked axes will be drawn for all selected rig elements.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleSchematicViewportVisibility, "Show Schematic Viewport", "Toggles the visibility of the viewport schematic", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::Y));
	UI_COMMAND(SwapModuleWithinAsset, "Swap Module (Asset)", "Swaps a module for all occurrences within this asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SwapModuleAcrossProject, "Swap Module (Project)", "Swaps a module for all occurrences in the project.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
