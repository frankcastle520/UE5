// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

#include "DMXControlConsoleCompactEditorModel.generated.h"

class UDMXControlConsole;
class UDMXControlConsoleEditorModel;
class UDMXControlConsoleEditorPlayMenuModel;


/** Model for the compact view of a control console */
UCLASS(Config = EditorPerProjectUserSettings)
class UDMXControlConsoleCompactEditorModel
	: public UObject
{
	GENERATED_BODY()

public:
	/** Sets the control console for the compact editor. Opens the editor if it is closed. */
	void SetControlConsole(UDMXControlConsole* ControlConsole);

	/** Restores the full editor for the current console. */
	void RestoreFullEditor();

	/** Stops playing DMX if the compact editor is currently playing DMX */
	void StopPlayingDMX();

	/** Returns true if the specified control console is currently displayed in the compact editor, without loading the console */
	bool IsUsingControlConsole(const UDMXControlConsole* ControlConsole) const;

	/** Loads the current control console, or gets it if it's already loaded. Returns nullptr if no control console is set. */
	UDMXControlConsole* LoadControlConsoleSynchronous() const;

	/** Delegate broadcast when the model changed */
	FSimpleMulticastDelegate OnModelChanged;

private:
	/** The control console currently in use in the compact editor */
	UPROPERTY(Config)
	TSoftObjectPtr<UDMXControlConsole> SoftControlConsole;
};
