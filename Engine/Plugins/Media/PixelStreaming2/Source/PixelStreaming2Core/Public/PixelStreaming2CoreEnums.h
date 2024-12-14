// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PixelStreaming2CoreEnums.generated.h"

/**
 * @brief The possible stream types Editor Streaming supports. 
 * 
 */
UENUM()
enum class EPixelStreaming2EditorStreamTypes : uint8
{
	/** `LevelEditorViewport` will stream just the level editor */
	LevelEditorViewport = 0,
	/** `Editor` will stream the full editor and any of its child windows */
	Editor = 1
};