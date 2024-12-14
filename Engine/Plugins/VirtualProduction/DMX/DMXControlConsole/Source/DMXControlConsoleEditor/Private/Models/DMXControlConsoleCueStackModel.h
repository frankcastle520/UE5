// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

struct FDMXControlConsoleCue;
class UDMXControlConsole;
class UDMXControlConsoleCueStack;
class UDMXControlConsoleData;
class UDMXControlConsoleEditorData;
class UDMXControlConsoleEditorLayouts;


namespace UE::DMX::Private
{
	/** Model for the control console cue stack */
	class FDMXControlConsoleCueStackModel
		: public TSharedFromThis<FDMXControlConsoleCueStackModel>
	{
	public:
		/** Constructor */
		FDMXControlConsoleCueStackModel(UDMXControlConsole* InControlConsole);

		/** Gets a reference to the control console data */
		UDMXControlConsoleData* GetControlConsoleData() const;

		/** Gets a reference to the control console editor data */
		UDMXControlConsoleEditorData* GetControlConsoleEditorData() const;

		/** Gets a reference to the control console editor layouts */
		UDMXControlConsoleEditorLayouts* GetControlConsoleEditorLayouts() const;

		/** Gets a reference to the control console cue stack */
		UDMXControlConsoleCueStack* GetControlConsoleCueStack() const;

		/** True if it is possible to add a new cue to the cue stack */
		bool IsAddNewCueButtonEnabled() const;

		/** True if the cue stack is ready to store new cue data */
		bool IsStoreCueButtonEnabled(const FDMXControlConsoleCue& Cue) const;

		/** Adds a new cue to the control console cue stack */
		void AddNewCue() const;

		/** Stores the given cue in the control console cue stack */
		void StoreCue(const FDMXControlConsoleCue& Cue) const;

		/** Recalls the given cue */
		void RecallCue(const FDMXControlConsoleCue& Cue) const;

		/** Clears the control console cue stack */
		void ClearCueStack() const;

	private:
		/** The control console used with this model */
		const TWeakObjectPtr<UDMXControlConsole> WeakControlConsole;
	};
}
