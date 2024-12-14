// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

namespace UE::AnimNext
{

// The Commands for the variable overrides menu
class FVariableOverrideCommands : public TCommands<FVariableOverrideCommands>
{
public:
	FVariableOverrideCommands();

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OverrideVariable;
	TSharedPtr<FUICommandInfo> ResetPropertyToDefault;
	TSharedPtr<FUICommandInfo> ClearOverride;
};

}