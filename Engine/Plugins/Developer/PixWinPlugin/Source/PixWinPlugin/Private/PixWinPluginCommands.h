// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "PixWinPluginStyle.h"
#include "Framework/Commands/Commands.h"

class FPixWinPluginCommands : public TCommands<FPixWinPluginCommands>
{
public:
	FPixWinPluginCommands()
		: TCommands<FPixWinPluginCommands>(TEXT("PixWinPlugin"), NSLOCTEXT("Contexts", "PixWinPlugin", "PixWin Plugin"), NAME_None, FPixWinPluginStyle::Get()->GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<class FUICommandInfo> CaptureFrameCommand;
};

#endif
