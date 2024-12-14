// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Run the SubmitTool .
 */
int RunSubmitTool(const TCHAR* Commandline, const FGuid& SessionID);
FString GetUserPrefsPath();
