// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditor3DViewportMode.h"

#define LOCTEXT_NAMESPACE "UUVEditor3DViewportMode"

const FEditorModeID UUVEditor3DViewportMode::EM_ModeID = TEXT("EM_UVEditor3DViewportModeId");

UUVEditor3DViewportMode::UUVEditor3DViewportMode()
{
	Info = FEditorModeInfo(
		EM_ModeID,
		LOCTEXT("ModeName", "UV 3D Viewport Dummy Mode"),
		FSlateIcon(),
		false);
}

#undef LOCTEXT_NAMESPACE