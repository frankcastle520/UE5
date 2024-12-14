// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Math/Color.h"

namespace UE::Editor::ContentBrowser::ExtensionUtils
{
	// Get the custom color of the given folder path (if any)
	CONTENTBROWSER_API TOptional<FLinearColor> GetFolderColor(const FName& FolderPath);

	// Set a custom color for the given folder path
	CONTENTBROWSER_API void SetFolderColor(const FName& FolderPath, const FLinearColor& FolderColor);
}
