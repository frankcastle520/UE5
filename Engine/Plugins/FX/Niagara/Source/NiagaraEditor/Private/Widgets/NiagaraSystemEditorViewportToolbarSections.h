// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"
#include "ToolMenuEntry.h"

class SNiagaraSystemViewport;

namespace UE::NiagaraSystemEditor
{
TSharedRef<SWidget> CreateShowMenuWidget(const TSharedRef<SNiagaraSystemViewport>& InNiagaraSystemEditorViewport, bool bInShowViewportStatsToggle = true);
FToolMenuEntry CreateShowSubmenu();

TSharedRef<SWidget> CreateMotionMenuWidget(const TSharedRef<SNiagaraSystemViewport>& InNiagaraSystemEditorViewport);
FToolMenuEntry CreateSettingsSubmenu();
}
