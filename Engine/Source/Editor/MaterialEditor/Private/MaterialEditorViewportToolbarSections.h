// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"
#include "ToolMenuEntry.h"

class SMaterialEditor3DPreviewViewport;

namespace UE::MaterialEditor
{
TSharedRef<SWidget> CreateShowMenuWidget(const TSharedRef<SMaterialEditor3DPreviewViewport>& InMaterialEditorViewport, bool bInShowViewportStatsToggle = true);
FToolMenuEntry CreateShowSubmenu();
void FillShowSubmenu(UToolMenu* InMenu, bool bInShowViewportStatsToggle = true);
}
