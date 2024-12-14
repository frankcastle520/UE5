// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Widgets/SWindow.h"

class FString;

class FSubmitToolUtils
{
public:
	static FString GetLocalAppDataPath();

	static void CopyDiagnosticFilesToClipboard(TConstArrayView<FString> Files);

	static void EnsureWindowIsInView(TSharedRef<SWindow> InWindow, bool bSingleWindow);
};
