// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "HAL/Platform.h"

// This is the platform independent entry point to the visual debugger
// It returns the error level
int32 RunChaosVisualDebugger(const TCHAR* CommandLine);