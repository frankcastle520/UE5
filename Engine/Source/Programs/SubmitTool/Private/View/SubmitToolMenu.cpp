// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolMenu.h"

#define LOCTEXT_NAMESPACE "FSubmitToolMenu"

void FSubmitToolMenu::FillMainMenuEntries(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("Main");
	{
		MenuBuilder.AddMenuEntry(FSubmitToolCommandList::Get().HelpCommandInfo);
		MenuBuilder.AddMenuEntry(FSubmitToolCommandList::Get().ExitCommandInfo);
	}
	MenuBuilder.EndSection();
}

#if !UE_BUILD_SHIPPING
void FSubmitToolMenu::FillDebugMenuEntries(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("Debug");
	{
		MenuBuilder.AddMenuEntry(FSubmitToolCommandList::Get().ForceCrashCommandInfo);
		MenuBuilder.AddMenuEntry(FSubmitToolCommandList::Get().WidgetReflectCommandInfo);
	}
	MenuBuilder.EndSection();
}
#endif

#undef LOCTEXT_NAMESPACE