// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"

#include "DMXControlConsoleEditorPlayMenuModel.generated.h"

enum class EDMXControlConsoleStopDMXMode : uint8;
class FUICommandList;
class UDMXControlConsole;
class UDMXControlConsoleData;
class UToolMenu;


/** Model for the control console play menu */
UCLASS(Transient)
class UDMXControlConsoleEditorPlayMenuModel
	: public UObject
{
	GENERATED_BODY()

public:
	/** Inits the PlayMenuModel from a control console and a command list. The PlayMenuModels actions are mapped to the specified command list */
	void Initialize(UDMXControlConsole* InControlConsole, const TSharedRef<FUICommandList>& InCommandList);

	/** Creates a play menu to the provided tool menu */
	void CreatePlayMenu(UToolMenu& InMenu);

	/** Returns the command list, or nullptr if no command list was set */
	const TSharedPtr<FUICommandList>& GetCommandList() const { return CommandList; }

	/** Returns true if DMX can be played */
	bool CanPlayDMX() const;

	/** Returns true if playing DMX can be resumed (requires the console to be paused and CanPlayDMX). */
	bool CanResumeDMX() const;

	/** Starts to play DMX */
	void PlayDMX();

	/** Returns true if playing DMX can be paused */
	bool CanPauseDMX() const;

	/** Pauses playing DMX. Current DMX values will still be sent at a lower rate. */
	void PauseDMX();

	/** Returns true if playing DMX can be stopped */
	bool CanStopPlayingDMX() const;

	/** Stops playing DMX */
	void StopPlayingDMX();

	/** Toggles between playing and pausing DMX */
	void TogglePlayPauseDMX();

	/** Toggles between playing and stopping DMX */
	void TogglePlayStopDMX();

	/** Sets the stop mode for the asset being edited */
	void SetStopDMXMode(EDMXControlConsoleStopDMXMode StopDMXMode);

	/** Returns true if the console uses the tested stop mode */
	bool IsUsingStopDMXMode(EDMXControlConsoleStopDMXMode TestStopMode) const;

	/** Returns true if the console is playing DMX */
	bool IsPlayingDMX() const;

	/** Returns true if the console is paused sending DMX */
	bool IsPausedDMX() const;

private:
	/** The control console data used with this model */
	UPROPERTY(NonTransactional)
	TObjectPtr<UDMXControlConsole> ControlConsole;

	/** The control console data used with this model */
	UPROPERTY(NonTransactional)
	TObjectPtr<UDMXControlConsoleData> ControlConsoleData;

	/** The command list for this PlayMenuModel */
	TSharedPtr<FUICommandList> CommandList;
};
