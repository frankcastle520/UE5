// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"
#include "ToolMenuEntry.h"

class SStaticMeshEditorViewport;
class SWidget;

namespace UE::StaticMeshEditor
{
FToolMenuEntry CreateLODSubmenu();
FToolMenuEntry CreateShowSubmenu();

TSharedRef<SWidget> GenerateLODMenuWidget(const TSharedPtr<SStaticMeshEditorViewport>& InStaticMeshEditorViewport);
TSharedRef<SWidget> GenerateShowMenuWidget(const TSharedPtr<SStaticMeshEditorViewport>& InStaticMeshEditorViewport);

void FillShowSubmenu(UToolMenu *InMenu);

FText GetLODMenuLabel(const TSharedPtr<SStaticMeshEditorViewport>& InStaticMeshEditorViewport);
}
